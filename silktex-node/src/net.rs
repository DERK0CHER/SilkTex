use anyhow::Result;
use futures::StreamExt;
use libp2p::{
    dcutr,
    gossipsub::{self, IdentTopic, MessageAuthenticity, ValidationMode},
    identify,
    kad::{self, store::MemoryStore},
    noise, relay,
    swarm::SwarmEvent,
    tcp, yamux, Multiaddr, StreamProtocol,
};
use libp2p_swarm::NetworkBehaviour;
use std::time::Duration;
use tokio::sync::mpsc;

const PROTOCOL_TAG_OP:   u8 = 0x01;
const PROTOCOL_TAG_SNAP: u8 = 0x02;
const PROTOCOL_TAG_REQ:  u8 = 0x03;

/// IPFS public bootstrap nodes — also serve as DHT entry points and relay candidates.
const BOOTSTRAP_ADDRS: &[&str] = &[
    "/dnsaddr/bootstrap.libp2p.io/p2p/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
    "/dnsaddr/bootstrap.libp2p.io/p2p/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa",
    "/dnsaddr/bootstrap.libp2p.io/p2p/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
    "/dnsaddr/bootstrap.libp2p.io/p2p/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
];

pub struct Network {
    cmd_tx: mpsc::Sender<NetCmd>,
}

enum NetCmd {
    Broadcast(Vec<u8>),
    BroadcastSnapshot(Vec<u8>),
}

#[derive(NetworkBehaviour)]
#[behaviour(prelude = "libp2p_swarm::derive_prelude")]
struct Behaviour {
    gossipsub: gossipsub::Behaviour,
    kad:       kad::Behaviour<MemoryStore>,
    identify:  identify::Behaviour,
    relay:     relay::client::Behaviour,
    dcutr:     dcutr::Behaviour,
}

impl Network {
    pub async fn start(
        session_id: String,
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<Self> {
        let (cmd_tx, cmd_rx) = mpsc::channel(64);
        tokio::spawn(run(session_id, update_tx, doc_id, cmd_rx, false));
        Ok(Self { cmd_tx })
    }

    /// Join an existing session. Returns `(Network, initial_snapshot)`.
    /// Snapshot is `None` if no peer responded within 15 s — joiner starts with empty doc.
    pub async fn join(
        session_id: String,
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<(Self, Option<Vec<u8>>)> {
        let (cmd_tx, cmd_rx) = mpsc::channel(64);
        let (snap_tx, mut snap_rx) = mpsc::channel::<Vec<u8>>(1);
        tokio::spawn(run_with_snap(session_id, update_tx, doc_id, cmd_rx, snap_tx));
        let snapshot = tokio::time::timeout(Duration::from_secs(15), snap_rx.recv())
            .await
            .ok()
            .flatten();
        Ok((Self { cmd_tx }, snapshot))
    }

    pub async fn broadcast_op(&self, update: Vec<u8>) {
        let mut msg = vec![PROTOCOL_TAG_OP];
        msg.extend_from_slice(&update);
        let _ = self.cmd_tx.send(NetCmd::Broadcast(msg)).await;
    }

    pub async fn broadcast_snapshot(&self, snapshot: Vec<u8>) {
        let mut msg = vec![PROTOCOL_TAG_SNAP];
        msg.extend_from_slice(&snapshot);
        let _ = self.cmd_tx.send(NetCmd::BroadcastSnapshot(msg)).await;
    }
}

fn build_swarm() -> Result<libp2p::Swarm<Behaviour>> {
    let gossipsub_cfg = gossipsub::ConfigBuilder::default()
        .heartbeat_interval(Duration::from_secs(1))
        .validation_mode(ValidationMode::Strict)
        .build()
        .map_err(|e| anyhow::anyhow!("gossipsub config: {e}"))?;

    let swarm = libp2p::SwarmBuilder::with_new_identity()
        .with_tokio()
        .with_tcp(tcp::Config::default(), noise::Config::new, yamux::Config::default)?
        .with_dns()?
        .with_relay_client(noise::Config::new, yamux::Config::default)?
        .with_behaviour(|key, relay_behaviour| {
            let local_peer_id = key.public().to_peer_id();

            let gossipsub = gossipsub::Behaviour::new(
                MessageAuthenticity::Signed(key.clone()),
                gossipsub_cfg,
            )?;

            // Use the IPFS Kademlia protocol so we can bootstrap off IPFS nodes.
            let kad_config = kad::Config::new(StreamProtocol::new("/ipfs/kad/1.0.0"));
            let mut kad = kad::Behaviour::with_config(
                local_peer_id,
                MemoryStore::new(local_peer_id),
                kad_config,
            );
            // Client mode: query and provide but don't serve DHT lookups for others.
            kad.set_mode(Some(kad::Mode::Client));

            let identify = identify::Behaviour::new(
                identify::Config::new("/silktex/0.1.0".into(), key.public()),
            );

            let dcutr = dcutr::Behaviour::new(local_peer_id);

            Ok(Behaviour { gossipsub, kad, identify, relay: relay_behaviour, dcutr })
        })?
        .with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(300)))
        .build();

    Ok(swarm)
}

async fn run(
    session_id: String,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    cmd_rx: mpsc::Receiver<NetCmd>,
    joiner: bool,
) {
    run_inner(session_id, update_tx, doc_id, cmd_rx, None, joiner).await;
}

async fn run_with_snap(
    session_id: String,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    cmd_rx: mpsc::Receiver<NetCmd>,
    snap_tx: mpsc::Sender<Vec<u8>>,
) {
    run_inner(session_id, update_tx, doc_id, cmd_rx, Some(snap_tx), true).await;
}

async fn run_inner(
    session_id: String,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    mut cmd_rx: mpsc::Receiver<NetCmd>,
    mut snap_tx: Option<mpsc::Sender<Vec<u8>>>,
    joiner: bool,
) {
    let mut swarm = match build_swarm() {
        Ok(s) => s,
        Err(e) => { tracing::error!("swarm build failed: {e}"); return; }
    };

    let topic = IdentTopic::new(&session_id);
    let session_key = kad::RecordKey::new(&session_id);

    if let Err(e) = swarm.behaviour_mut().gossipsub.subscribe(&topic) {
        tracing::error!("subscribe failed: {e}");
        return;
    }

    if let Err(e) = swarm.listen_on("/ip4/0.0.0.0/tcp/0".parse().unwrap()) {
        tracing::error!("listen failed: {e}");
        return;
    }

    // Dial bootstrap nodes and pre-populate the Kademlia routing table so that
    // kad.bootstrap() has peers to query immediately (avoids "No known peers").
    for addr_str in BOOTSTRAP_ADDRS {
        if let Ok(addr) = addr_str.parse::<Multiaddr>() {
            // Extract the /p2p/<PeerId> component and seed the routing table.
            let peer_id = addr.iter().find_map(|p| {
                if let libp2p::multiaddr::Protocol::P2p(id) = p { Some(id) } else { None }
            });
            if let Some(peer_id) = peer_id {
                swarm.behaviour_mut().kad.add_address(&peer_id, addr.clone());
            }
            swarm.dial(addr).ok();
        }
    }

    // Now that the routing table has bootstrap peers, kick off DHT bootstrap.
    swarm.behaviour_mut().kad.bootstrap().ok();

    // Session peers (those subscribed to our gossipsub topic).
    // We track these separately so bootstrap/DHT nodes don't inflate the count.
    let mut session_peers: std::collections::HashSet<libp2p::PeerId> = Default::default();

    let mut bootstrapped = false;
    let mut relay_listening = false;

    // If bootstrap hasn't completed in 10 s, try providing / querying anyway.
    let bootstrap_deadline = tokio::time::sleep(Duration::from_secs(10));
    tokio::pin!(bootstrap_deadline);

    // Re-query the DHT every 10 s so we find peers that registered after us.
    let mut discover_tick = tokio::time::interval(Duration::from_secs(10));
    discover_tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    discover_tick.tick().await; // skip the immediate first tick

    // Retry the snapshot request every second until gossipsub mesh forms.
    let mut snap_req_pending = false;
    let mut snap_retry_tick = tokio::time::interval(Duration::from_secs(1));
    snap_retry_tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

    tracing::info!("p2p node started, bootstrapping DHT…");

    loop {
        tokio::select! {
            Some(cmd) = cmd_rx.recv() => {
                let data = match cmd {
                    NetCmd::Broadcast(d) | NetCmd::BroadcastSnapshot(d) => d,
                };
                if let Err(e) = swarm.behaviour_mut().gossipsub.publish(topic.clone(), data) {
                    tracing::debug!("publish op: {e}");
                }
            }

            // Fallback: if bootstrap never fires, force the provide+query after 10 s.
            _ = &mut bootstrap_deadline, if !bootstrapped => {
                tracing::warn!("DHT bootstrap timed out — trying to provide/query anyway");
                bootstrapped = true;
                swarm.behaviour_mut().kad.start_providing(session_key.clone()).ok();
                swarm.behaviour_mut().kad.get_providers(session_key.clone());
            }

            _ = discover_tick.tick() => {
                if bootstrapped {
                    // Refresh our provider record and re-query.
                    swarm.behaviour_mut().kad.start_providing(session_key.clone()).ok();
                    swarm.behaviour_mut().kad.get_providers(session_key.clone());
                }
            }

            // Retry snapshot request until the gossipsub mesh is ready.
            _ = snap_retry_tick.tick(), if snap_req_pending && snap_tx.is_some() => {
                let req = vec![PROTOCOL_TAG_REQ];
                match swarm.behaviour_mut().gossipsub.publish(topic.clone(), req) {
                    Ok(_) => {
                        snap_req_pending = false;
                        tracing::info!("snapshot request sent");
                    }
                    Err(e) => tracing::debug!("snap req retry ({e})"),
                }
            }

            event = swarm.select_next_some() => {
                match event {
                    // ── gossipsub messages ──────────────────────────────────────
                    SwarmEvent::Behaviour(BehaviourEvent::Gossipsub(
                        gossipsub::Event::Message { message, .. }
                    )) => {
                        let data = message.data;
                        if data.is_empty() { continue; }
                        match data[0] {
                            PROTOCOL_TAG_OP => {
                                let _ = update_tx.send((doc_id.clone(), data[1..].to_vec())).await;
                            }
                            PROTOCOL_TAG_SNAP => {
                                snap_req_pending = false;
                                if let Some(tx) = snap_tx.take() {
                                    let _ = tx.send(data[1..].to_vec()).await;
                                }
                            }
                            PROTOCOL_TAG_REQ => {
                                let _ = update_tx.send(("__snap_req__".into(), vec![])).await;
                            }
                            _ => {}
                        }
                    }

                    // ── gossipsub peer tracking (session peers only) ────────────
                    SwarmEvent::Behaviour(BehaviourEvent::Gossipsub(
                        gossipsub::Event::Subscribed { peer_id, topic: t }
                    )) => {
                        if t == topic.hash() {
                            tracing::info!("session peer joined: {peer_id}");
                            session_peers.insert(peer_id);
                            let count = session_peers.len();
                            let _ = update_tx.send((
                                "__peer_count__".into(),
                                (count as u64).to_be_bytes().to_vec(),
                            )).await;
                            if joiner && snap_tx.is_some() {
                                snap_req_pending = true;
                            }
                        }
                    }

                    SwarmEvent::Behaviour(BehaviourEvent::Gossipsub(
                        gossipsub::Event::Unsubscribed { peer_id, topic: t }
                    )) => {
                        if t == topic.hash() {
                            tracing::info!("session peer left: {peer_id}");
                            session_peers.remove(&peer_id);
                            let count = session_peers.len();
                            let _ = update_tx.send((
                                "__peer_count__".into(),
                                (count as u64).to_be_bytes().to_vec(),
                            )).await;
                        }
                    }

                    // ── identify: feed addresses into Kademlia; detect relays ───
                    SwarmEvent::Behaviour(BehaviourEvent::Identify(
                        identify::Event::Received { peer_id, info, .. }
                    )) => {
                        tracing::debug!("identify: peer={peer_id} agent={}", info.agent_version);
                        for addr in &info.listen_addrs {
                            swarm.behaviour_mut().kad.add_address(&peer_id, addr.clone());
                        }

                        // If this peer supports relay hop, reserve a slot through it.
                        const RELAY_HOP: &str = "/libp2p/circuit/relay/0.2.0/hop";
                        if !relay_listening
                            && info.protocols.iter().any(|p| p.as_ref() == RELAY_HOP)
                        {
                            if let Some(relay_addr) = info.listen_addrs.iter().find(|a| {
                                a.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::Tcp(_)))
                            }) {
                                let mut circuit = relay_addr.clone();
                                circuit.push(libp2p::multiaddr::Protocol::P2p(peer_id));
                                circuit.push(libp2p::multiaddr::Protocol::P2pCircuit);
                                if swarm.listen_on(circuit.clone()).is_ok() {
                                    relay_listening = true;
                                    tracing::info!("circuit relay listen: {circuit}");
                                }
                            }
                        }
                    }

                    // ── Kademlia ────────────────────────────────────────────────
                    SwarmEvent::Behaviour(BehaviourEvent::Kad(
                        kad::Event::OutboundQueryProgressed { result, .. }
                    )) => {
                        match result {
                            kad::QueryResult::Bootstrap(Ok(kad::BootstrapOk {
                                num_remaining: 0, ..
                            })) => {
                                if !bootstrapped {
                                    bootstrapped = true;
                                    tracing::info!("DHT bootstrap complete — announcing session and searching for peers");
                                    swarm.behaviour_mut().kad
                                        .start_providing(session_key.clone()).ok();
                                    swarm.behaviour_mut().kad
                                        .get_providers(session_key.clone());
                                }
                            }

                            kad::QueryResult::Bootstrap(Err(e)) => {
                                tracing::warn!("DHT bootstrap step failed: {e:?}");
                                if !bootstrapped {
                                    bootstrapped = true;
                                    swarm.behaviour_mut().kad
                                        .start_providing(session_key.clone()).ok();
                                    swarm.behaviour_mut().kad
                                        .get_providers(session_key.clone());
                                }
                            }

                            kad::QueryResult::StartProviding(Ok(ref r)) => {
                                tracing::info!("session announced in DHT (key={:?})", r.key);
                            }
                            kad::QueryResult::StartProviding(Err(ref e)) => {
                                tracing::warn!("DHT provide failed: {e:?}");
                            }

                            kad::QueryResult::GetProviders(Ok(
                                kad::GetProvidersOk::FoundProviders { ref providers, .. }
                            )) => {
                                tracing::info!("DHT found {} provider(s) for session", providers.len());
                                for peer_id in providers.clone() {
                                    if peer_id != *swarm.local_peer_id() {
                                        tracing::info!("dialing session peer: {peer_id}");
                                        swarm.behaviour_mut().gossipsub
                                            .add_explicit_peer(&peer_id);
                                        swarm.dial(peer_id).ok();
                                    }
                                }
                                if joiner && snap_tx.is_some() {
                                    snap_req_pending = true;
                                }
                            }

                            kad::QueryResult::GetProviders(Ok(
                                kad::GetProvidersOk::FinishedWithNoAdditionalRecord { .. }
                            )) => {
                                tracing::debug!("DHT provider query finished — no peers found yet");
                            }

                            _ => {}
                        }
                    }

                    // ── relay ───────────────────────────────────────────────────
                    SwarmEvent::Behaviour(BehaviourEvent::Relay(
                        relay::client::Event::ReservationReqAccepted { relay_peer_id, .. }
                    )) => {
                        tracing::info!("relay reservation accepted via {relay_peer_id}");
                        // Re-announce now that we have a routable circuit address.
                        if bootstrapped {
                            swarm.behaviour_mut().kad
                                .start_providing(session_key.clone()).ok();
                        }
                    }

                    SwarmEvent::Behaviour(BehaviourEvent::Relay(ev)) => {
                        tracing::debug!("relay: {ev:?}");
                    }

                    SwarmEvent::ConnectionEstablished { peer_id, endpoint, .. } => {
                        tracing::debug!("connected: {peer_id} via {}", endpoint.get_remote_address());
                    }
                    SwarmEvent::ConnectionClosed { peer_id, .. } => {
                        tracing::debug!("disconnected: {peer_id}");
                        // Remove from session peers if they dropped without unsubscribing.
                        if session_peers.remove(&peer_id) {
                            let count = session_peers.len();
                            let _ = update_tx.send((
                                "__peer_count__".into(),
                                (count as u64).to_be_bytes().to_vec(),
                            )).await;
                        }
                    }

                    SwarmEvent::OutgoingConnectionError { peer_id, error, .. } => {
                        tracing::warn!("outgoing connection failed {peer_id:?}: {error}");
                    }

                    _ => {}
                }
            }
        }
    }
}
