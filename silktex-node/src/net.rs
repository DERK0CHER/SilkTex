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
/// Bumped to /1 when the HELLO payload became the join secret: a /0 build and a
/// /1 build now fail ALPN negotiation instead of half-connecting.
const ALPN: &[u8] = b"silktex/collab/1";

const TAG_HELLO: u8 = 0x00; // join secret from joiner → host
const TAG_OP:    u8 = 0x01; // Loro CRDT operation
const TAG_SNAP:  u8 = 0x02; // full Loro snapshot
const TAG_META:  u8 = 0x03; // JSON metadata: cursor position, display name

const MAX_TICKET_BYTES: usize = 8 * 1024;
const MAX_WIRE_PAYLOAD: usize = 16 * 1024 * 1024;
const MAX_PEERS: usize = 8;
const SECRET_LEN: usize = 32;

/* ------------------------------------------------------------------ */
/* Session ticket                                                       */
/* ------------------------------------------------------------------ */

/// The session code is this struct serialised with postcard + base32.
/// The joiner decodes it to know how to reach the host and to prove it was
/// given the code: `secret` is a fresh CSPRNG value per session and is the
/// only thing that authorises a joiner.  Postcard writes fixed-size byte
/// arrays inline, so this adds exactly 32 bytes to the code.
#[derive(Serialize, Deserialize)]
struct Ticket {
    addr: EndpointAddr,
    secret: [u8; SECRET_LEN],
}

/// Hand-written so that debug-formatting a ticket can never leak the secret.
impl std::fmt::Debug for Ticket {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Ticket")
            .field("addr", &self.addr)
            .field("secret", &"<redacted>")
            .finish()
    }
}

impl Ticket {
    fn encode(&self) -> Result<String> {
        let bytes = postcard::to_stdvec(self).map_err(|e| anyhow::anyhow!("ticket encode: {e}"))?;
        let mut s = BASE32_NOPAD.encode(&bytes);
        s.make_ascii_lowercase();
        Ok(s)
    }

    fn decode(s: &str) -> Result<Self> {
        // Reject huge raw inputs before allocating to prevent memory exhaustion.
        if s.len() > MAX_TICKET_BYTES * 2 {
            anyhow::bail!("ticket too large");
        }
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

/// Compare two byte strings without leaking *where* they differ through timing.
/// Only the length is allowed to affect the running time (the secret's length is
/// public); the byte loop is branch-free.  Never index into the network payload.
fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
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
        let mut secret = [0u8; SECRET_LEN];
        getrandom::fill(&mut secret).map_err(|e| anyhow::anyhow!("join secret: {e}"))?;
        let ticket = Ticket { addr: endpoint.addr(), secret };
        let session_code = ticket.encode()?;
        // Never log the code or the secret: the code *is* the credential.
        tracing::info!("session host: id={}", endpoint.id());

        let peers: PeerMap = Arc::new(Mutex::new(HashMap::new()));
        tokio::spawn(host_run(endpoint, peers, secret, update_tx, doc_id, cmd_rx));

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
            endpoint, ticket.addr, ticket.secret, update_tx, doc_id, cmd_rx, snap_tx,
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
    secret: [u8; SECRET_LEN],
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
                for (id, tx) in map.iter() {
                    let mut envelope = Vec::with_capacity(1 + data.len());
                    envelope.push(tag);
                    envelope.extend_from_slice(&data);
                    if tx.try_send(envelope).is_err() {
                        tracing::warn!("outgoing queue full for peer {id}; dropping message");
                    }
                }
            }

            // Incoming: accept a new joiner connection.
            incoming_opt = endpoint.accept() => {
                let Some(incoming) = incoming_opt else { break };
                let accepting = match incoming.accept() {
                    Ok(a)  => a,
                    Err(e) => { tracing::warn!("accept error: {e}"); continue; }
                };

                // Finish the handshake in its own task so a slow or hostile
                // handshake cannot stall broadcasting to the other peers.
                let update_tx2 = update_tx.clone();
                let doc_id2    = doc_id.clone();
                let peers2     = peers.clone();
                tokio::spawn(async move {
                    let conn: Connection = match accepting.await {
                        Ok(c)  => c,
                        Err(e) => { tracing::warn!("connecting error: {e}"); return; }
                    };
                    let peer_id = conn.remote_id();

                    // Authenticate first.  Nothing below this point runs for a
                    // caller that cannot prove it holds the session code: no peer
                    // map entry, no snapshot request, no document data.  Bounded
                    // in time so a silent connection cannot pin this task forever;
                    // on every failure path `conn` is dropped, which closes it.
                    let hello = tokio::time::timeout(Duration::from_secs(10), async {
                        let (send, mut recv) = conn.accept_bi().await?;
                        let (tag, payload) = read_msg(&mut recv).await?;
                        anyhow::Ok((send, recv, tag, payload))
                    })
                    .await;
                    let (send, recv, tag, payload) = match hello {
                        Ok(Ok(v))  => v,
                        Ok(Err(e)) => { tracing::warn!("rejecting peer {peer_id}: handshake failed: {e}"); return; }
                        Err(_)     => { tracing::warn!("rejecting peer {peer_id}: handshake timed out"); return; }
                    };
                    if tag != TAG_HELLO {
                        tracing::warn!("rejecting peer {peer_id}: expected HELLO, got tag {tag:#04x}");
                        return;
                    }
                    // Constant-time; a wrong-length payload simply fails to match.
                    if !ct_eq(&payload, &secret) {
                        tracing::warn!("rejecting peer {peer_id}: invalid join secret");
                        return;
                    }

                    let (peer_tx, peer_rx) = mpsc::channel::<Vec<u8>>(64);
                    {
                        let mut map = lock_peers(&peers2);
                        if map.len() >= MAX_PEERS {
                            tracing::warn!("rejecting peer {peer_id}: peer limit reached");
                            return;
                        }
                        map.insert(peer_id, peer_tx);
                    }
                    tracing::info!("joiner connected: {peer_id}");

                    // Only now ask the app for a snapshot for the new joiner.
                    let _ = update_tx2.send(("__snap_req__".into(), vec![])).await;
                    emit_peer_count(&update_tx2, &peers2).await;

                    if let Err(e) = host_handle_peer(send, recv, peer_rx, update_tx2.clone(), doc_id2, peers2.clone(), peer_id).await {
                        tracing::debug!("peer {peer_id} disconnected: {e}");
                    }
                    lock_peers(&peers2).remove(&peer_id);
                    tracing::info!("joiner disconnected: {peer_id}");
                    emit_peer_count(&update_tx2, &peers2).await;
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

/// Runs only after the HELLO carried a valid join secret; `send`/`recv` are the
/// already-accepted streams of that authenticated connection.
async fn host_handle_peer(
    mut send: impl AsyncWriteExt + Unpin + Send,
    mut recv: impl AsyncReadExt + Unpin + Send,
    mut peer_rx: mpsc::Receiver<Vec<u8>>,
    update_tx: mpsc::Sender<(String, Vec<u8>)>,
    doc_id: String,
    peers: PeerMap,
    my_peer_id: EndpointId,
) -> Result<()> {
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
                                if tx.try_send(env).is_err() {
                                    tracing::warn!("relay queue full for peer {id}; dropping op");
                                }
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
                                if tx.try_send(env).is_err() {
                                    tracing::warn!("relay queue full for peer {id}; dropping meta");
                                }
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
    secret: [u8; SECRET_LEN],
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
    // The HELLO payload is the join secret from the session code; the host sends
    // nothing until it matches.
    let (mut send, mut recv) = match conn.open_bi().await {
        Ok(s)  => s,
        Err(e) => {
            tracing::error!("open_bi failed: {e}");
            endpoint.close().await;
            return;
        }
    };
    if let Err(e) = write_msg(&mut send, TAG_HELLO, &secret).await {
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

#[cfg(test)]
mod tests {
    use super::{
        ct_eq, inject_peer_id, read_msg, write_msg, Ticket, MAX_TICKET_BYTES, MAX_WIRE_PAYLOAD,
        SECRET_LEN, TAG_OP,
    };
    use serde::{Deserialize, Serialize};
    use tokio::io::AsyncWriteExt;

    // ---- Ticket::decode security ----------------------------------------

    #[test]
    fn ticket_decode_rejects_oversized_raw_input() {
        // Pre-filter guard: huge input rejected before allocation.
        let huge = "a".repeat(MAX_TICKET_BYTES * 2 + 1);
        let err = Ticket::decode(&huge).unwrap_err();
        assert!(err.to_string().contains("ticket too large"), "got: {err}");
    }

    #[test]
    fn ticket_decode_rejects_at_post_filter_limit() {
        // Input fits the pre-filter but cleaned string exceeds MAX_TICKET_BYTES.
        let just_over = "a".repeat(MAX_TICKET_BYTES + 1);
        let err = Ticket::decode(&just_over).unwrap_err();
        assert!(err.to_string().contains("ticket too large"), "got: {err}");
    }

    #[test]
    fn ticket_decode_empty_input_errors() {
        // Empty input: empty cleaned string → postcard decode fails (not a panic).
        let err = Ticket::decode("").unwrap_err();
        assert!(!err.to_string().is_empty());
    }

    // ---- Ticket shape ---------------------------------------------------

    /// Mirrors the `secret` field of `Ticket`.  `Ticket` itself cannot be built
    /// in a unit test (it needs a live `EndpointAddr`), so pin down the part of
    /// the encoding this change added: postcard writes a fixed-size byte array
    /// inline, with no length prefix, so the secret costs exactly 32 bytes.
    #[derive(Serialize, Deserialize, PartialEq, Debug)]
    struct SecretProbe {
        secret: [u8; SECRET_LEN],
    }

    #[test]
    fn ticket_secret_is_32_fixed_bytes() {
        let probe = SecretProbe { secret: [0xA5; SECRET_LEN] };
        let bytes = postcard::to_stdvec(&probe).unwrap();
        assert_eq!(bytes.len(), SECRET_LEN);
        let back: SecretProbe = postcard::from_bytes(&bytes).unwrap();
        assert_eq!(back, probe);
    }

    #[test]
    fn ticket_decode_rejects_truncated_secret() {
        // A ticket-shaped blob whose secret is one byte short must fail to
        // decode rather than deserialise into something usable.
        let short = postcard::to_stdvec(&[0u8; SECRET_LEN - 1]).unwrap();
        let mut code = data_encoding::BASE32_NOPAD.encode(&short);
        code.make_ascii_lowercase();
        assert!(Ticket::decode(&code).is_err());
    }

    // ---- Constant-time secret comparison --------------------------------

    #[test]
    fn ct_eq_accepts_identical_secrets() {
        let a = [7u8; SECRET_LEN];
        let b = [7u8; SECRET_LEN];
        assert!(ct_eq(&a, &b));
    }

    #[test]
    fn ct_eq_rejects_single_bit_difference() {
        let a = [0u8; SECRET_LEN];
        let mut b = [0u8; SECRET_LEN];
        b[SECRET_LEN - 1] = 0x01;
        assert!(!ct_eq(&a, &b));
        // Differing in the very first byte must be rejected just the same.
        let mut c = [0u8; SECRET_LEN];
        c[0] = 0x80;
        assert!(!ct_eq(&a, &c));
    }

    #[test]
    fn ct_eq_rejects_wrong_length_payload() {
        let secret = [3u8; SECRET_LEN];
        // Empty payload: what a pre-secret (ALPN /0) joiner would send.
        assert!(!ct_eq(&[], &secret));
        // Correct prefix, short: must not be accepted, must not panic.
        assert!(!ct_eq(&secret[..SECRET_LEN - 1], &secret));
        // Correct prefix, too long.
        let mut long = secret.to_vec();
        long.push(0);
        assert!(!ct_eq(&long, &secret));
    }

    #[test]
    fn ct_eq_empty_slices_are_equal() {
        assert!(ct_eq(&[], &[]));
    }

    // ---- inject_peer_id -------------------------------------------------

    #[test]
    fn inject_peer_id_inserts_field() {
        let payload = br#"{"type":"cursor","offset":5}"#;
        let result = inject_peer_id(payload, "peer42");
        let val: serde_json::Value = serde_json::from_slice(&result).unwrap();
        assert_eq!(val["peer_id"], "peer42");
        assert_eq!(val["type"], "cursor");
        assert_eq!(val["offset"], 5);
    }

    #[test]
    fn inject_peer_id_preserves_existing_fields() {
        let payload = br#"{"a":1,"b":2}"#;
        let result = inject_peer_id(payload, "p");
        let val: serde_json::Value = serde_json::from_slice(&result).unwrap();
        assert_eq!(val["a"], 1);
        assert_eq!(val["b"], 2);
        assert_eq!(val["peer_id"], "p");
    }

    #[test]
    fn inject_peer_id_overwrites_existing_peer_id() {
        let payload = br#"{"peer_id":"old"}"#;
        let result = inject_peer_id(payload, "new");
        let val: serde_json::Value = serde_json::from_slice(&result).unwrap();
        assert_eq!(val["peer_id"], "new");
    }

    #[test]
    fn inject_peer_id_passthrough_invalid_json() {
        let payload = b"not json at all";
        let result = inject_peer_id(payload, "p");
        assert_eq!(result, payload);
    }

    #[test]
    fn inject_peer_id_passthrough_non_object() {
        let payload = b"[1,2,3]";
        let result = inject_peer_id(payload, "p");
        let val: serde_json::Value = serde_json::from_slice(&result).unwrap();
        assert!(val.is_array());
    }

    // ---- Wire framing ---------------------------------------------------

    #[tokio::test]
    async fn wire_roundtrip_op_tag() {
        let (mut w, mut r) = tokio::io::duplex(256);
        write_msg(&mut w, TAG_OP, b"hello world").await.unwrap();
        let (tag, payload) = read_msg(&mut r).await.unwrap();
        assert_eq!(tag, TAG_OP);
        assert_eq!(payload, b"hello world");
    }

    #[tokio::test]
    async fn wire_roundtrip_empty_payload() {
        let (mut w, mut r) = tokio::io::duplex(64);
        write_msg(&mut w, TAG_OP, &[]).await.unwrap();
        let (tag, payload) = read_msg(&mut r).await.unwrap();
        assert_eq!(tag, TAG_OP);
        assert!(payload.is_empty());
    }

    #[tokio::test]
    async fn read_msg_rejects_oversized_len_header() {
        let (mut w, mut r) = tokio::io::duplex(64);
        w.write_u8(TAG_OP).await.unwrap();
        w.write_u32((MAX_WIRE_PAYLOAD + 1) as u32).await.unwrap();
        drop(w);
        let err = read_msg(&mut r).await.unwrap_err();
        assert!(err.to_string().contains("too large"), "got: {err}");
    }

    #[tokio::test]
    async fn write_msg_rejects_oversized_payload() {
        let (mut w, _r) = tokio::io::duplex(64);
        let large = vec![0u8; MAX_WIRE_PAYLOAD + 1];
        let err = write_msg(&mut w, TAG_OP, &large).await.unwrap_err();
        assert!(err.to_string().contains("too large"), "got: {err}");
    }
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
