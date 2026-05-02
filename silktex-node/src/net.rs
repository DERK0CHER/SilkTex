use anyhow::Result;
use bytes::Bytes;
use data_encoding::BASE32_NOPAD;
use futures::TryStreamExt;
use iroh::{
    address_lookup::memory::MemoryLookup,
    endpoint::presets,
    Endpoint, EndpointAddr, PublicKey,
};
use iroh_gossip::{
    api::Event,
    net::{Gossip, GOSSIP_ALPN},
    proto::TopicId,
};
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::time::Duration;
use tokio::sync::mpsc;

const PROTOCOL_TAG_OP:   u8 = 0x01;
const PROTOCOL_TAG_SNAP: u8 = 0x02;
const PROTOCOL_TAG_REQ:  u8 = 0x03;


/// A session ticket that carries the topic ID and the host's endpoint address.
/// Serialised with postcard + base32 so it's a single copy-pasteable string.
#[derive(Debug, Serialize, Deserialize)]
struct Ticket {
    topic: TopicId,
    peers: Vec<EndpointAddr>,
}

impl Ticket {
    fn encode(&self) -> String {
        let bytes = postcard::to_stdvec(self).expect("ticket serialize");
        let mut s = BASE32_NOPAD.encode(&bytes);
        s.make_ascii_lowercase();
        s
    }

    fn decode(s: &str) -> Result<Self> {
        let bytes = BASE32_NOPAD
            .decode(s.trim().to_ascii_uppercase().as_bytes())
            .map_err(|e| anyhow::anyhow!("ticket base32: {e}"))?;
        postcard::from_bytes(&bytes)
            .map_err(|e| anyhow::anyhow!("ticket decode: {e}"))
    }
}

/* ------------------------------------------------------------------ */
/* Public Network handle                                               */
/* ------------------------------------------------------------------ */

pub struct Network {
    cmd_tx: mpsc::Sender<NetCmd>,
}

enum NetCmd {
    Broadcast(Vec<u8>),
    BroadcastSnapshot(Vec<u8>),
}

impl Network {
    /// Start hosting a new session.
    /// Returns `(Network, session_code)` — the session_code is the base32 ticket
    /// that the joining peer pastes into the "Join" dialog.
    pub async fn start(
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<(Self, String)> {
        let (cmd_tx, cmd_rx) = mpsc::channel(64);

        let memory_lookup = MemoryLookup::new();
        let endpoint = build_endpoint(memory_lookup.clone()).await?;

        // Random topic: use a fresh Ed25519 public key's bytes as 32 random bytes.
        let topic_id = TopicId::from_bytes(iroh::SecretKey::generate().to_bytes());
        let ticket = Ticket { topic: topic_id, peers: vec![endpoint.addr()] };
        let session_code = ticket.encode();

        tracing::info!("session host: endpoint={} code={}", endpoint.id(), session_code);

        // Host has no bootstrap peers — it waits for joiners to connect via the ticket.
        tokio::spawn(run_node(
            endpoint, memory_lookup, topic_id, vec![],
            update_tx, doc_id, cmd_rx, false, None,
        ));

        Ok((Self { cmd_tx }, session_code))
    }

    /// Join an existing session using the base32 ticket as `session_code`.
    /// Returns `(Network, initial_snapshot)` — snapshot is `None` if the host
    /// didn't respond within 15 s.
    pub async fn join(
        session_code: String,
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<(Self, Option<Vec<u8>>)> {
        let ticket = Ticket::decode(&session_code)?;
        let topic_id = ticket.topic;

        let memory_lookup = MemoryLookup::new();
        // Pre-populate address book with the host's endpoint address so iroh
        // can reach the host by ID through the relay URL in the ticket.
        let bootstrap_peers: Vec<PublicKey> = ticket.peers
            .iter()
            .map(|a| a.id)
            .collect();
        for peer_addr in ticket.peers {
            memory_lookup.add_endpoint_info(peer_addr);
        }
        let endpoint = build_endpoint(memory_lookup.clone()).await?;

        tracing::info!("session join: endpoint={}", endpoint.id());

        let (cmd_tx, cmd_rx) = mpsc::channel(64);
        let (snap_tx, mut snap_rx) = mpsc::channel::<Vec<u8>>(1);

        tokio::spawn(run_node(
            endpoint, memory_lookup, topic_id, bootstrap_peers,
            update_tx, doc_id, cmd_rx, true, Some(snap_tx),
        ));

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

/* ------------------------------------------------------------------ */
/* Endpoint setup helpers                                              */
/* ------------------------------------------------------------------ */

/// Build an iroh Endpoint connected to n0's relay infrastructure.
async fn build_endpoint(memory_lookup: MemoryLookup) -> Result<Endpoint> {
    let endpoint = Endpoint::builder(presets::N0)
        .address_lookup(memory_lookup)
        .bind()
        .await?;

    // Wait until we have a home relay so our address (with relay URL) is sharable.
    endpoint.online().await;
    Ok(endpoint)
}

/* ------------------------------------------------------------------ */
/* Gossip event loop                                                   */
/* ------------------------------------------------------------------ */

async fn run_node(
    endpoint: Endpoint,
    _memory_lookup: MemoryLookup,
    topic_id: TopicId,
    bootstrap_peers: Vec<PublicKey>,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    mut cmd_rx: mpsc::Receiver<NetCmd>,
    joiner: bool,
    mut snap_tx: Option<mpsc::Sender<Vec<u8>>>,
) {
    let gossip = Gossip::builder().spawn(endpoint.clone());

    // Router accepts incoming gossip connections from remote peers.
    let router = iroh::protocol::Router::builder(endpoint.clone())
        .accept(GOSSIP_ALPN, gossip.clone())
        .spawn();

    // Host passes vec![] — it waits for joiners.
    // Joiner passes the host's endpoint ID — iroh reaches it via the relay URL
    // that was pre-loaded into the MemoryLookup from the ticket.
    let topic = match gossip.subscribe_and_join(topic_id, bootstrap_peers).await {
        Ok(t) => t,
        Err(e) => {
            tracing::error!("subscribe_and_join failed: {e}");
            router.shutdown().await.ok();
            return;
        }
    };

    tracing::info!("joined gossip topic");
    let (sender, mut receiver) = topic.split();

    let mut session_peers: HashSet<PublicKey> = HashSet::new();
    let mut snap_req_pending = false;
    let mut snap_retry_tick = tokio::time::interval(Duration::from_secs(1));
    snap_retry_tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

    loop {
        tokio::select! {
            Some(cmd) = cmd_rx.recv() => {
                let data = match cmd {
                    NetCmd::Broadcast(d) | NetCmd::BroadcastSnapshot(d) => d,
                };
                if let Err(e) = sender.broadcast(Bytes::from(data)).await {
                    tracing::debug!("broadcast: {e}");
                }
            }

            _ = snap_retry_tick.tick(), if snap_req_pending && snap_tx.is_some() => {
                let req = Bytes::from_static(&[PROTOCOL_TAG_REQ]);
                match sender.broadcast(req).await {
                    Ok(_) => {
                        snap_req_pending = false;
                        tracing::info!("snapshot request sent");
                    }
                    Err(e) => tracing::debug!("snap req retry: {e}"),
                }
            }

            event = receiver.try_next() => {
                let event = match event {
                    Ok(Some(e)) => e,
                    Ok(None)    => break,
                    Err(e) => { tracing::warn!("gossip stream error: {e}"); break; }
                };

                match event {
                    Event::Received(msg) => {
                        let data = msg.content.to_vec();
                        if data.is_empty() { continue; }
                        match data[0] {
                            PROTOCOL_TAG_OP => {
                                let _ = update_tx
                                    .send((doc_id.clone(), data[1..].to_vec()))
                                    .await;
                            }
                            PROTOCOL_TAG_SNAP => {
                                snap_req_pending = false;
                                if let Some(tx) = snap_tx.take() {
                                    let _ = tx.send(data[1..].to_vec()).await;
                                }
                            }
                            PROTOCOL_TAG_REQ => {
                                let _ = update_tx
                                    .send(("__snap_req__".into(), vec![]))
                                    .await;
                            }
                            _ => {}
                        }
                    }

                    Event::NeighborUp(peer) => {
                        tracing::info!("neighbor up: {peer}");
                        session_peers.insert(peer);
                        emit_peer_count(&update_tx, session_peers.len()).await;
                        if joiner && snap_tx.is_some() {
                            snap_req_pending = true;
                        }
                    }

                    Event::NeighborDown(peer) => {
                        tracing::info!("neighbor down: {peer}");
                        session_peers.remove(&peer);
                        emit_peer_count(&update_tx, session_peers.len()).await;
                    }

                    _ => {}
                }
            }
        }
    }

    router.shutdown().await.ok();
    tracing::info!("p2p node stopped");
}

async fn emit_peer_count(update_tx: &mpsc::Sender<(String, Vec<u8>)>, count: usize) {
    let _ = update_tx
        .send((
            "__peer_count__".into(),
            (count as u64).to_be_bytes().to_vec(),
        ))
        .await;
}
