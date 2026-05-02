use anyhow::Result;
use data_encoding::BASE32_NOPAD;
use iroh::{
    address_lookup::memory::MemoryLookup,
    endpoint::{presets, Connection},
    Endpoint, EndpointAddr, EndpointId,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::mpsc;

/// Application-layer protocol identifier (negotiated via TLS ALPN).
const ALPN: &[u8] = b"silktex/collab/0";

const TAG_HELLO: u8 = 0x00; // initial handshake byte from joiner → host
const TAG_OP:    u8 = 0x01; // Loro CRDT operation
const TAG_SNAP:  u8 = 0x02; // full Loro snapshot
const TAG_META:  u8 = 0x03; // JSON metadata: cursor position, display name

const MAX_TICKET_BYTES: usize = 8 * 1024;
const MAX_WIRE_PAYLOAD: usize = 16 * 1024 * 1024;
const MAX_PEERS: usize = 8;

/* ------------------------------------------------------------------ */
/* Session ticket                                                       */
/* ------------------------------------------------------------------ */

/// The session code is this struct serialised with postcard + base32.
/// The joiner decodes it to know how to reach the host.
#[derive(Debug, Serialize, Deserialize)]
struct Ticket {
    addr: EndpointAddr,
}

impl Ticket {
    fn encode(&self) -> Result<String> {
        let bytes = postcard::to_stdvec(self).map_err(|e| anyhow::anyhow!("ticket encode: {e}"))?;
        let mut s = BASE32_NOPAD.encode(&bytes);
        s.make_ascii_lowercase();
        Ok(s)
    }

    fn decode(s: &str) -> Result<Self> {
        // Strip whitespace and non-base32 chars (handles copy-paste noise).
        let cleaned: String = s
            .chars()
            .filter(|c| matches!(c, 'a'..='z' | 'A'..='Z' | '2'..='7'))
            .collect::<String>()
            .to_ascii_uppercase();
        if cleaned.len() > MAX_TICKET_BYTES {
            anyhow::bail!("ticket too large");
        }
        let bytes = BASE32_NOPAD
            .decode(cleaned.as_bytes())
            .map_err(|e| anyhow::anyhow!("ticket invalid (use the copy button): {e}"))?;
        postcard::from_bytes(&bytes).map_err(|e| anyhow::anyhow!("ticket decode: {e}"))
    }
}

/* ------------------------------------------------------------------ */
/* Wire framing: [tag: u8 | len: u32be | payload: bytes]              */
/* ------------------------------------------------------------------ */

async fn write_msg(w: &mut (impl AsyncWriteExt + Unpin), tag: u8, payload: &[u8]) -> Result<()> {
    if payload.len() > MAX_WIRE_PAYLOAD {
        anyhow::bail!("message too large: {} bytes", payload.len());
    }
    w.write_u8(tag).await?;
    w.write_u32(payload.len() as u32).await?;
    w.write_all(payload).await?;
    Ok(())
}

async fn read_msg(r: &mut (impl AsyncReadExt + Unpin)) -> Result<(u8, Vec<u8>)> {
    let tag = r.read_u8().await?;
    let len = r.read_u32().await? as usize;
    if len > MAX_WIRE_PAYLOAD {
        anyhow::bail!("message too large: {len} bytes");
    }
    let mut buf = vec![0u8; len];
    r.read_exact(&mut buf).await?;
    Ok((tag, buf))
}

/* ------------------------------------------------------------------ */
/* Public handle                                                        */
/* ------------------------------------------------------------------ */

pub struct Network {
    cmd_tx: mpsc::Sender<NetCmd>,
    /// Our own endpoint ID as a display string; embed in outgoing meta messages.
    pub local_peer_id: String,
}

enum NetCmd {
    Op(Vec<u8>),
    Snapshot(Vec<u8>),
    Meta(Vec<u8>),
    Shutdown,
}

/// Per-peer outgoing channel map (EndpointId → sender).
type PeerMap = Arc<Mutex<HashMap<EndpointId, mpsc::Sender<Vec<u8>>>>>;

fn lock_peers(peers: &PeerMap) -> MutexGuard<'_, HashMap<EndpointId, mpsc::Sender<Vec<u8>>>> {
    peers.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

impl Network {
    /// Start hosting.  Returns `(Network, session_code)`.
    pub async fn start(
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<(Self, String)> {
        let (cmd_tx, cmd_rx) = mpsc::channel(64);

        let memory_lookup = MemoryLookup::new();
        let endpoint = build_endpoint(memory_lookup, true).await?;

        let local_peer_id = format!("{}", endpoint.id());
        let ticket = Ticket { addr: endpoint.addr() };
        let session_code = ticket.encode()?;
        tracing::info!("session host: id={} code={}", endpoint.id(), session_code);

        let peers: PeerMap = Arc::new(Mutex::new(HashMap::new()));
        tokio::spawn(host_run(endpoint, peers, update_tx, doc_id, cmd_rx));

        Ok((Self { cmd_tx, local_peer_id }, session_code))
    }

    /// Join an existing session.  Returns `(Network, initial_snapshot)`.
    pub async fn join(
        session_code: String,
        update_tx: mpsc::Sender<(String, Vec<u8>)>,
        doc_id: String,
    ) -> Result<(Self, Option<Vec<u8>>)> {
        let ticket = Ticket::decode(&session_code)?;

        let memory_lookup = MemoryLookup::new();
        memory_lookup.add_endpoint_info(ticket.addr.clone());
        let endpoint = build_endpoint(memory_lookup, false).await?;

        let local_peer_id = format!("{}", endpoint.id());
        tracing::info!("session join: id={}", endpoint.id());

        let (cmd_tx, cmd_rx) = mpsc::channel(64);
        let (snap_tx, mut snap_rx) = mpsc::channel::<Vec<u8>>(1);

        tokio::spawn(joiner_run(
            endpoint, ticket.addr, update_tx, doc_id, cmd_rx, snap_tx,
        ));

        let snapshot = tokio::time::timeout(Duration::from_secs(20), snap_rx.recv())
            .await
            .ok()
            .flatten();

        Ok((Self { cmd_tx, local_peer_id }, snapshot))
    }

    pub async fn broadcast_op(&self, data: Vec<u8>) {
        let _ = self.cmd_tx.send(NetCmd::Op(data)).await;
    }

    pub async fn broadcast_snapshot(&self, data: Vec<u8>) {
        let _ = self.cmd_tx.send(NetCmd::Snapshot(data)).await;
    }

    pub async fn broadcast_meta(&self, data: Vec<u8>) {
        let _ = self.cmd_tx.send(NetCmd::Meta(data)).await;
    }

    pub async fn shutdown(&self) {
        let _ = self.cmd_tx.send(NetCmd::Shutdown).await;
    }
}

impl Drop for Network {
    fn drop(&mut self) {
        let _ = self.cmd_tx.try_send(NetCmd::Shutdown);
    }
}

/* ------------------------------------------------------------------ */
/* Endpoint factory                                                     */
/* ------------------------------------------------------------------ */

async fn build_endpoint(memory_lookup: MemoryLookup, accepting: bool) -> Result<Endpoint> {
    let mut builder = Endpoint::builder(presets::N0).address_lookup(memory_lookup);
    if accepting {
        builder = builder.alpns(vec![ALPN.to_vec()]);
    }
    let endpoint = builder.bind().await?;
    endpoint.online().await;
    tracing::info!("endpoint online: relays={:?}", endpoint.addr().relay_urls().collect::<Vec<_>>());
    Ok(endpoint)
}

/* ------------------------------------------------------------------ */
/* Meta helper: inject "peer_id" into a JSON object payload           */
/* ------------------------------------------------------------------ */

fn inject_peer_id(payload: &[u8], peer_id: &str) -> Vec<u8> {
    let Ok(mut val) = serde_json::from_slice::<serde_json::Value>(payload) else {
        return payload.to_vec();
    };
    if let Some(obj) = val.as_object_mut() {
        obj.insert("peer_id".to_string(), serde_json::json!(peer_id));
    }
    serde_json::to_vec(&val).unwrap_or_else(|_| payload.to_vec())
}

/* ------------------------------------------------------------------ */
/* Host                                                                 */
/* ------------------------------------------------------------------ */

async fn host_run(
    endpoint: Endpoint,
    peers: PeerMap,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    mut cmd_rx: mpsc::Receiver<NetCmd>,
) {
    loop {
        tokio::select! {
            // Outgoing: broadcast op/snapshot/meta from local user to all peers.
            cmd_opt = cmd_rx.recv() => {
                let Some(cmd) = cmd_opt else { break };
                let (tag, data) = match cmd {
                    NetCmd::Op(d)       => (TAG_OP,   d),
                    NetCmd::Snapshot(d) => (TAG_SNAP, d),
                    NetCmd::Meta(d)     => (TAG_META, d),
                    NetCmd::Shutdown    => break,
                };
                let map = lock_peers(&peers);
                for tx in map.values() {
                    let mut envelope = Vec::with_capacity(1 + data.len());
                    envelope.push(tag);
                    envelope.extend_from_slice(&data);
                    let _ = tx.try_send(envelope);
                }
            }

            // Incoming: accept a new joiner connection.
            incoming_opt = endpoint.accept() => {
                let Some(incoming) = incoming_opt else { break };
                let accepting = match incoming.accept() {
                    Ok(a)  => a,
                    Err(e) => { tracing::warn!("accept error: {e}"); continue; }
                };
                let conn: Connection = match accepting.await {
                    Ok(c)  => c,
                    Err(e) => { tracing::warn!("connecting error: {e}"); continue; }
                };
                let peer_id = conn.remote_id();
                if lock_peers(&peers).len() >= MAX_PEERS {
                    tracing::warn!("rejecting peer {peer_id}: peer limit reached");
                    continue;
                }
                tracing::info!("joiner connected: {peer_id}");

                let (peer_tx, peer_rx) = mpsc::channel::<Vec<u8>>(64);
                lock_peers(&peers).insert(peer_id, peer_tx);

                emit_peer_count(&update_tx, &peers).await;

                let update_tx2 = update_tx.clone();
                let doc_id2    = doc_id.clone();
                let peers2     = peers.clone();
                let peers3     = peers.clone();
                let update_tx3 = update_tx.clone();
                tokio::spawn(async move {
                    if let Err(e) = host_handle_peer(conn, peer_rx, update_tx2, doc_id2, peers3, peer_id).await {
                        tracing::debug!("peer {peer_id} disconnected: {e}");
                    }
                    lock_peers(&peers2).remove(&peer_id);
                    tracing::info!("joiner disconnected: {peer_id}");
                    emit_peer_count(&update_tx3, &peers2).await;
                });
            }
        }
    }

    endpoint.close().await;
    tracing::info!("host stopped");
}

/// Called when peer count changes; sends the count to the app layer.
async fn emit_peer_count(tx: &mpsc::Sender<(String, Vec<u8>)>, peers: &PeerMap) {
    let count = lock_peers(peers).len() as u64;
    let _ = tx.send(("__peer_count__".into(), count.to_be_bytes().to_vec())).await;
}

async fn host_handle_peer(
    conn: Connection,
    mut peer_rx: mpsc::Receiver<Vec<u8>>,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    peers: PeerMap,
    my_peer_id: EndpointId,
) -> Result<()> {
    // The joiner opens the bi-directional stream and writes TAG_HELLO first
    // (required by iroh: the opener must write before the acceptor can see the stream).
    let (mut send, mut recv) = conn.accept_bi().await?;

    // Consume the HELLO, then ask the app for a snapshot to send to the new joiner.
    let (tag, _) = read_msg(&mut recv).await?;
    if tag == TAG_HELLO {
        let _ = update_tx.send(("__snap_req__".into(), vec![])).await;
    }

    let peer_id_str = format!("{my_peer_id}");

    loop {
        tokio::select! {
            // Forward snapshot / ops / meta destined for this peer.
            Some(envelope) = peer_rx.recv() => {
                let tag = envelope[0];
                write_msg(&mut send, tag, &envelope[1..]).await?;
            }

            // Receive messages from this peer: forward to app and relay to others.
            result = read_msg(&mut recv) => {
                let (tag, payload) = result?;
                match tag {
                    TAG_OP => {
                        let _ = update_tx.send((doc_id.clone(), payload.clone())).await;
                        // Relay to all other peers (star topology).
                        let map = lock_peers(&peers);
                        for (id, tx) in map.iter() {
                            if *id != my_peer_id {
                                let mut env = Vec::with_capacity(1 + payload.len());
                                env.push(TAG_OP);
                                env.extend_from_slice(&payload);
                                let _ = tx.try_send(env);
                            }
                        }
                    }
                    TAG_META => {
                        // Inject the source peer's ID into the JSON before forwarding.
                        let relayed = inject_peer_id(&payload, &peer_id_str);
                        let _ = update_tx.send(("__meta__".into(), relayed.clone())).await;
                        let map = lock_peers(&peers);
                        for (id, tx) in map.iter() {
                            if *id != my_peer_id {
                                let mut env = Vec::with_capacity(1 + relayed.len());
                                env.push(TAG_META);
                                env.extend_from_slice(&relayed);
                                let _ = tx.try_send(env);
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Joiner                                                               */
/* ------------------------------------------------------------------ */

async fn joiner_run(
    endpoint: Endpoint,
    host_addr: EndpointAddr,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    mut cmd_rx: mpsc::Receiver<NetCmd>,
    snap_tx: mpsc::Sender<Vec<u8>>,
) {
    let conn = match connect_with_retry(&endpoint, &host_addr).await {
        Ok(c)  => c,
        Err(e) => {
            tracing::error!("could not connect to host: {e}");
            endpoint.close().await;
            return;
        }
    };

    tracing::info!("connected to host");

    // Notify the app that we have 1 peer.
    let _ = update_tx.send(("__peer_count__".into(), 1u64.to_be_bytes().to_vec())).await;

    // Joiner opens the bi-directional stream and writes TAG_HELLO first.
    // iroh requires the opener to write before the acceptor can see the stream.
    let (mut send, mut recv) = match conn.open_bi().await {
        Ok(s)  => s,
        Err(e) => {
            tracing::error!("open_bi failed: {e}");
            endpoint.close().await;
            return;
        }
    };
    if let Err(e) = write_msg(&mut send, TAG_HELLO, &[]).await {
        tracing::error!("hello failed: {e}");
        endpoint.close().await;
        return;
    }

    let mut snap_tx = Some(snap_tx);

    loop {
        tokio::select! {
            // Forward outgoing ops / snapshots / meta to the host.
            cmd_opt = cmd_rx.recv() => {
                let Some(cmd) = cmd_opt else { break };
                let (tag, data) = match cmd {
                    NetCmd::Op(d)       => (TAG_OP,   d),
                    NetCmd::Snapshot(d) => (TAG_SNAP, d),
                    NetCmd::Meta(d)     => (TAG_META, d),
                    NetCmd::Shutdown    => break,
                };
                if let Err(e) = write_msg(&mut send, tag, &data).await {
                    tracing::debug!("send error: {e}");
                    break;
                }
            }

            // Receive snapshot / ops / meta from the host.
            result = read_msg(&mut recv) => {
                let (tag, payload) = match result {
                    Ok(r)  => r,
                    Err(e) => { tracing::debug!("recv error: {e}"); break; }
                };
                match tag {
                    TAG_SNAP => {
                        if let Some(tx) = snap_tx.take() {
                            let _ = tx.send(payload).await;
                        }
                    }
                    TAG_OP => {
                        let _ = update_tx.send((doc_id.clone(), payload)).await;
                    }
                    TAG_META => {
                        // Peer_id is already injected by the host relay.
                        let _ = update_tx.send(("__meta__".into(), payload)).await;
                    }
                    _ => {}
                }
            }
        }
    }

    let _ = update_tx.send(("__peer_count__".into(), 0u64.to_be_bytes().to_vec())).await;
    endpoint.close().await;
    tracing::info!("joiner stopped");
}

async fn connect_with_retry(endpoint: &Endpoint, host_addr: &EndpointAddr) -> Result<Connection> {
    let mut last_err = anyhow::anyhow!("no attempts");
    for attempt in 0..5 {
        if attempt > 0 {
            tokio::time::sleep(Duration::from_secs(3)).await;
            tracing::info!("connect retry {attempt}/4…");
        }
        match endpoint.connect(host_addr.clone(), ALPN).await {
            Ok(conn) => {
                tracing::info!("iroh connect succeeded on attempt {attempt}");
                return Ok(conn);
            }
            Err(e) => {
                tracing::warn!("connect attempt {attempt}: {e}");
                last_err = e.into();
            }
        }
    }
    Err(last_err)
}
