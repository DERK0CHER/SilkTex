use anyhow::Result;
use futures::StreamExt;
use libp2p::{
    gossipsub::{self, IdentTopic, MessageAuthenticity, ValidationMode},
    mdns,
    noise,
    swarm::SwarmEvent,
    tcp, yamux,
};
use libp2p_swarm::NetworkBehaviour;
use std::time::Duration;
use tokio::sync::mpsc;

const PROTOCOL_TAG_OP:   u8 = 0x01;
const PROTOCOL_TAG_SNAP: u8 = 0x02;
const PROTOCOL_TAG_REQ:  u8 = 0x03;   /* snapshot request */

pub struct Network {
    cmd_tx: mpsc::Sender<NetCmd>,
}

enum NetCmd {
    Broadcast(Vec<u8>),
    /// Send our snapshot to all peers (called when we receive a REQ).
    BroadcastSnapshot(Vec<u8>),
}

#[derive(NetworkBehaviour)]
#[behaviour(prelude = "libp2p_swarm::derive_prelude")]
struct Behaviour {
    gossipsub: gossipsub::Behaviour,
    mdns:      mdns::tokio::Behaviour,
}

impl Network {
    /// Create a new session (host role).
    pub async fn start(
        session_id: String,
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<Self> {
        let (cmd_tx, cmd_rx) = mpsc::channel(64);
        tokio::spawn(run(session_id, update_tx, doc_id, cmd_rx, false));
        Ok(Self { cmd_tx })
    }

    /// Join an existing session (joiner role).
    ///
    /// Returns `(Network, initial_snapshot)`.  The snapshot may be `None` if no
    /// peers responded within the timeout — the joiner starts with an empty doc.
    pub async fn join(
        session_id: String,
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<(Self, Option<Vec<u8>>)> {
        let (cmd_tx, cmd_rx) = mpsc::channel(64);
        let (snap_tx, mut snap_rx) = mpsc::channel::<Vec<u8>>(1);

        tokio::spawn(run_with_snap(session_id, update_tx, doc_id, cmd_rx, snap_tx));

        let snapshot = tokio::time::timeout(Duration::from_secs(5), snap_rx.recv())
            .await
            .ok()
            .flatten();

        Ok((Self { cmd_tx }, snapshot))
    }

    /// Broadcast a Loro update to all peers on the topic.
    pub async fn broadcast_op(&self, update: Vec<u8>) {
        let mut msg = vec![PROTOCOL_TAG_OP];
        msg.extend_from_slice(&update);
        let _ = self.cmd_tx.send(NetCmd::Broadcast(msg)).await;
    }

    /// Broadcast our current snapshot in response to a peer request.
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
        .with_behaviour(|key| {
            let gossipsub = gossipsub::Behaviour::new(
                MessageAuthenticity::Signed(key.clone()),
                gossipsub_cfg,
            )?;
            let mdns = mdns::tokio::Behaviour::new(
                mdns::Config::default(),
                key.public().to_peer_id(),
            )?;
            Ok(Behaviour { gossipsub, mdns })
        })?
        .with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(120)))
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
    run_with_snap_opt(session_id, update_tx, doc_id, cmd_rx, None, joiner).await;
}

async fn run_with_snap(
    session_id: String,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    cmd_rx: mpsc::Receiver<NetCmd>,
    snap_tx: mpsc::Sender<Vec<u8>>,
) {
    run_with_snap_opt(session_id, update_tx, doc_id, cmd_rx, Some(snap_tx), true).await;
}

async fn run_with_snap_opt(
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

    if let Err(e) = swarm.behaviour_mut().gossipsub.subscribe(&topic) {
        tracing::error!("subscribe failed: {e}");
        return;
    }

    if let Err(e) = swarm.listen_on("/ip4/0.0.0.0/tcp/0".parse().unwrap()) {
        tracing::error!("listen failed: {e}");
        return;
    }

    let mut peer_count: usize = 0;

    loop {
        tokio::select! {
            Some(cmd) = cmd_rx.recv() => {
                let data = match cmd {
                    NetCmd::Broadcast(d) | NetCmd::BroadcastSnapshot(d) => d,
                };
                if let Err(e) = swarm.behaviour_mut().gossipsub.publish(topic.clone(), data) {
                    tracing::warn!("publish: {e}");
                }
            }

            event = swarm.select_next_some() => {
                match event {
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
                                /* First snapshot from a peer — hand it to the joiner task. */
                                if let Some(tx) = snap_tx.take() {
                                    let _ = tx.send(data[1..].to_vec()).await;
                                }
                            }
                            PROTOCOL_TAG_REQ => {
                                /* A peer wants our snapshot.  We can't call back into doc
                                 * from here, so we forward the request to the command handler
                                 * via the update channel with a sentinel doc_id. */
                                let _ = update_tx.send(
                                    ("__snap_req__".into(), vec![])
                                ).await;
                            }
                            _ => {}
                        }
                    }

                    SwarmEvent::ConnectionEstablished { .. } => {
                        peer_count += 1;
                        let _ = update_tx.send((
                            "__peer_count__".into(),
                            (peer_count as u64).to_be_bytes().to_vec(),
                        )).await;
                    }

                    SwarmEvent::ConnectionClosed { .. } => {
                        peer_count = peer_count.saturating_sub(1);
                        let _ = update_tx.send((
                            "__peer_count__".into(),
                            (peer_count as u64).to_be_bytes().to_vec(),
                        )).await;
                    }

                    SwarmEvent::Behaviour(BehaviourEvent::Mdns(
                        mdns::Event::Discovered(peers)
                    )) => {
                        for (peer_id, addr) in peers {
                            swarm.behaviour_mut().gossipsub.add_explicit_peer(&peer_id);
                            swarm.add_peer_address(peer_id, addr);
                        }
                        /* Send snapshot request now that we know at least one peer exists. */
                        if joiner && snap_tx.is_some() {
                            let req = vec![PROTOCOL_TAG_REQ];
                            let _ = swarm.behaviour_mut().gossipsub.publish(topic.clone(), req);
                        }
                    }

                    SwarmEvent::Behaviour(BehaviourEvent::Mdns(
                        mdns::Event::Expired(peers)
                    )) => {
                        for (peer_id, _) in peers {
                            swarm.behaviour_mut().gossipsub.remove_explicit_peer(&peer_id);
                        }
                    }

                    _ => {}
                }
            }
        }
    }
}
