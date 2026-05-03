mod doc;
mod net;

use anyhow::Result;
use doc::Document;
use net::Network;
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::io::{self, Write};
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::sync::{Mutex, mpsc};

/* ------------------------------------------------------------------ */
/* IPC protocol types                                                   */
/* ------------------------------------------------------------------ */

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(tag = "cmd", rename_all = "snake_case")]
enum Command {
    CreateSession { doc_id: String, content: String },
    JoinSession   { doc_id: String, session_id: String },
    Op            { doc_id: String, retain: usize, insert: Option<String>, delete: Option<usize> },
    SetName       { name: String },
    Cursor        { doc_id: String, offset: usize },
    Shutdown,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(deny_unknown_fields)]
pub struct TextOp {
    pub retain: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub insert: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub delete: Option<usize>,
}

#[derive(Serialize)]
#[serde(tag = "event", rename_all = "snake_case")]
enum Event<'a> {
    SessionReady  { doc_id: &'a str, session_id: &'a str },
    RemoteOp      { doc_id: String,  #[serde(flatten)] op: TextOp },
    Snapshot      { doc_id: &'a str, content: String },
    PeerCount     { doc_id: &'a str, count: usize },
    RemoteCursor  { doc_id: &'a str, peer_id: String, offset: usize },
    PeerName      { peer_id: String, name: String },
    Error         { msg: String },
}

fn emit(ev: &Event<'_>) -> bool {
    match serde_json::to_string(ev) {
        Ok(s) => {
            let mut stdout = io::stdout().lock();
            match writeln!(stdout, "{s}") {
                Ok(()) => true,
                Err(e) if e.kind() == io::ErrorKind::BrokenPipe => false,
                Err(e) => {
                    eprintln!("emit error: {e}");
                    false
                }
            }
        }
        Err(e) => {
            eprintln!("emit error: {e}");
            false
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "warn".into()),
        )
        .init();

    /* Security boundary: silktex-node is intentionally content-only.
     * It accepts document text/CRDT updates over stdin and iroh, but has no
     * filesystem commands, file paths, shell execution, or project traversal API.
     *
     * Channel for incoming Loro updates from the network layer.
     * The doc_id "__snap_req__" is a sentinel: the network is telling us
     * that a peer wants our snapshot. */
    let (net_tx, mut net_rx) = mpsc::channel::<(String, Vec<u8>)>(64);

    let doc = Arc::new(Mutex::new(Document::new()));
    let mut network: Option<Network> = None;
    let mut current_doc_id = String::new();

    /* Spawn a task that processes incoming network updates. */
    let doc2    = doc.clone();
    let (apply_tx, mut apply_rx) = mpsc::channel::<(String, TextOp)>(64);
    tokio::spawn(async move {
        while let Some((doc_id, update)) = net_rx.recv().await {
            if doc_id == "__snap_req__" {
                let _ = apply_tx.send(("__snap_req__".into(), TextOp {
                    retain: 0, insert: None, delete: None,
                })).await;
                continue;
            }
            if doc_id == "__peer_count__" {
                let count = if update.len() >= 8 {
                    let mut count_bytes = [0u8; 8];
                    count_bytes.copy_from_slice(&update[..8]);
                    u64::from_be_bytes(count_bytes) as usize
                } else { 0 };
                let _ = apply_tx.send(("__peer_count__".into(), TextOp {
                    retain: count, insert: None, delete: None,
                })).await;
                continue;
            }
            if doc_id == "__meta__" {
                /* Pass raw JSON bytes through as the insert field of a sentinel TextOp. */
                let json_str = String::from_utf8_lossy(&update).into_owned();
                let _ = apply_tx.send(("__meta__".into(), TextOp {
                    retain: 0, insert: Some(json_str), delete: None,
                })).await;
                continue;
            }
            let mut d = doc2.lock().await;
            match d.apply_update(&update) {
                Ok(Some(op)) => { let _ = apply_tx.send((doc_id, op)).await; }
                Ok(None)     => {}
                Err(e)       => eprintln!("apply_update: {e}"),
            }
        }
    });

    /* IPC command loop — read JSON lines from stdin. */
    let stdin = BufReader::new(tokio::io::stdin());
    let mut lines = stdin.lines();

    'main_loop: loop {
        tokio::select! {
            /* Emit events arising from remote ops. */
            Some((doc_id, op)) = apply_rx.recv() => {
                if doc_id == "__snap_req__" {
                    /* A peer requested our snapshot. */
                    if let Some(net) = &network {
                        let snap = doc.lock().await.export_snapshot();
                        net.broadcast_snapshot(snap).await;
                    }
                } else if doc_id == "__peer_count__" {
                    if !emit(&Event::PeerCount { doc_id: &current_doc_id, count: op.retain }) {
                        break 'main_loop;
                    }
                } else if doc_id == "__meta__" {
                    /* Cursor / name update from a remote peer — peer_id already injected. */
                    let json_str = op.insert.as_deref().unwrap_or("");
                    if let Ok(val) = serde_json::from_str::<serde_json::Value>(json_str) {
                        if let Some(obj) = val.as_object() {
                            let kind    = obj.get("type").and_then(|v| v.as_str()).unwrap_or("");
                            let peer_id = obj.get("peer_id").and_then(|v| v.as_str()).unwrap_or("?").to_string();
                            match kind {
                                "cursor" => {
                                    let offset = obj.get("offset").and_then(|v| v.as_u64()).unwrap_or(0) as usize;
                                    if !emit(&Event::RemoteCursor { doc_id: &current_doc_id, peer_id, offset }) {
                                        break 'main_loop;
                                    }
                                }
                                "name" => {
                                    let name = obj.get("name").and_then(|v| v.as_str()).unwrap_or("?").to_string();
                                    if !emit(&Event::PeerName { peer_id, name }) {
                                        break 'main_loop;
                                    }
                                }
                                _ => {}
                            }
                        }
                    }
                } else {
                    if !emit(&Event::RemoteOp { doc_id, op }) {
                        break 'main_loop;
                    }
                }
            }

            /* Read the next command from the C side. */
            Ok(Some(line)) = lines.next_line() => {
                let line = line.trim().to_owned();
                if line.is_empty() { continue; }

                let cmd: Command = match serde_json::from_str(&line) {
                    Ok(c) => c,
                    Err(e) => {
                        if !emit(&Event::Error { msg: format!("parse: {e}") }) {
                            break 'main_loop;
                        }
                        continue;
                    }
                };

                match cmd {
                    Command::CreateSession { doc_id, content } => {
                        if let Some(net) = network.take() {
                            net.shutdown().await;
                        }
                        doc.lock().await.set_content(&content)?;
                        current_doc_id = doc_id.clone();

                        let (net, session_code) =
                            Network::start(net_tx.clone(), doc_id.clone()).await?;
                        if !emit(&Event::SessionReady {
                            doc_id: &doc_id,
                            session_id: &session_code,
                        }) {
                            net.shutdown().await;
                            break 'main_loop;
                        }
                        network = Some(net);
                    }

                    Command::JoinSession { doc_id, session_id } => {
                        if let Some(net) = network.take() {
                            net.shutdown().await;
                        }
                        current_doc_id = doc_id.clone();

                        let (net, snapshot) = Network::join(
                            session_id.clone(), net_tx.clone(), doc_id.clone(),
                        ).await?;

                        if let Some(snap) = snapshot {
                            let mut d = doc.lock().await;
                            if let Err(e) = d.apply_snapshot(&snap) {
                                if !emit(&Event::Error { msg: format!("snapshot: {e}") }) {
                                    net.shutdown().await;
                                    break 'main_loop;
                                }
                            } else {
                                let content = d.get_content();
                                drop(d);
                                if !emit(&Event::Snapshot { doc_id: &doc_id, content }) {
                                    net.shutdown().await;
                                    break 'main_loop;
                                }
                            }
                        }
                        /* Emit SessionReady for the joiner so the C layer enters session state. */
                        if !emit(&Event::SessionReady { doc_id: &doc_id, session_id: &session_id }) {
                            net.shutdown().await;
                            break 'main_loop;
                        }
                        network = Some(net);
                    }

                    Command::Op { doc_id, retain, insert, delete } => {
                        if doc_id != current_doc_id {
                            if !emit(&Event::Error {
                                msg: "op rejected: document is not the active collaboration session".into(),
                            }) {
                                break 'main_loop;
                            }
                            continue;
                        }
                        let op = TextOp { retain, insert, delete };
                        match doc.lock().await.apply_op(&op) {
                            Ok(update) => {
                                if let Some(net) = &network {
                                    net.broadcast_op(update).await;
                                }
                            }
                            Err(e) => {
                                if !emit(&Event::Error { msg: e.to_string() }) {
                                    break 'main_loop;
                                }
                            }
                        }
                        let _ = doc_id;
                    }

                    Command::SetName { name } => {
                        if let Some(net) = &network {
                            let payload = serde_json::to_vec(&json!({
                                "type": "name",
                                "peer_id": net.local_peer_id,
                                "name": name,
                            })).unwrap_or_default();
                            net.broadcast_meta(payload).await;
                        }
                    }

                    Command::Cursor { doc_id, offset } => {
                        if doc_id == current_doc_id {
                            if let Some(net) = &network {
                                let payload = serde_json::to_vec(&json!({
                                    "type": "cursor",
                                    "peer_id": net.local_peer_id,
                                    "offset": offset,
                                })).unwrap_or_default();
                                net.broadcast_meta(payload).await;
                            }
                        }
                    }

                    Command::Shutdown => break 'main_loop,
                }
            }

            else => break 'main_loop,
        }
    }

    if let Some(net) = network.take() {
        net.shutdown().await;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{Command, Event, TextOp};

    // ---- Command deserialization ----------------------------------------

    #[test]
    fn parse_create_session() {
        let json = r#"{"cmd":"create_session","doc_id":"doc1","content":"hello"}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::CreateSession { ref doc_id, ref content }
            if doc_id == "doc1" && content == "hello"));
    }

    #[test]
    fn parse_join_session() {
        let json = r#"{"cmd":"join_session","doc_id":"d","session_id":"code"}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::JoinSession { .. }));
    }

    #[test]
    fn parse_op_insert_only() {
        let json = r#"{"cmd":"op","doc_id":"d","retain":3,"insert":"hi"}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::Op { retain: 3, ref insert, delete: None, .. }
            if insert.as_deref() == Some("hi")));
    }

    #[test]
    fn parse_op_delete_only() {
        let json = r#"{"cmd":"op","doc_id":"d","retain":0,"delete":5}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::Op { delete: Some(5), insert: None, .. }));
    }

    #[test]
    fn parse_set_name() {
        let json = r#"{"cmd":"set_name","name":"Alice"}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::SetName { ref name } if name == "Alice"));
    }

    #[test]
    fn parse_cursor() {
        let json = r#"{"cmd":"cursor","doc_id":"d","offset":42}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::Cursor { offset: 42, .. }));
    }

    #[test]
    fn parse_shutdown() {
        let json = r#"{"cmd":"shutdown"}"#;
        let cmd: Command = serde_json::from_str(json).unwrap();
        assert!(matches!(cmd, Command::Shutdown));
    }

    #[test]
    fn parse_unknown_command_errors() {
        let json = r#"{"cmd":"launch_missiles","doc_id":"x"}"#;
        let result: Result<Command, _> = serde_json::from_str(json);
        assert!(result.is_err());
    }

    #[test]
    fn parse_unknown_field_errors() {
        // deny_unknown_fields rejects extra keys on struct variants.
        let json = r#"{"cmd":"set_name","name":"Alice","extra":"injected"}"#;
        let result: Result<Command, _> = serde_json::from_str(json);
        assert!(result.is_err(), "expected error for unknown field");
    }

    #[test]
    fn parse_malformed_json_errors() {
        let result: Result<Command, _> = serde_json::from_str("{bad json");
        assert!(result.is_err());
    }

    // ---- TextOp serialization ------------------------------------------

    #[test]
    fn text_op_skips_none_fields_in_json() {
        let op = TextOp { retain: 0, insert: None, delete: None };
        let s = serde_json::to_string(&op).unwrap();
        let val: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert!(val.get("insert").is_none(), "insert should be absent");
        assert!(val.get("delete").is_none(), "delete should be absent");
    }

    #[test]
    fn text_op_roundtrip() {
        let op = TextOp { retain: 5, insert: Some("hello".into()), delete: Some(3) };
        let s = serde_json::to_string(&op).unwrap();
        let op2: TextOp = serde_json::from_str(&s).unwrap();
        assert_eq!(op2.retain, 5);
        assert_eq!(op2.insert.as_deref(), Some("hello"));
        assert_eq!(op2.delete, Some(3));
    }

    // ---- Event serialization -------------------------------------------

    #[test]
    fn event_session_ready_format() {
        let ev = Event::SessionReady { doc_id: "doc1", session_id: "code123" };
        let s = serde_json::to_string(&ev).unwrap();
        let val: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(val["event"], "session_ready");
        assert_eq!(val["doc_id"], "doc1");
        assert_eq!(val["session_id"], "code123");
    }

    #[test]
    fn event_remote_op_flattens_text_op() {
        let op = TextOp { retain: 3, insert: Some("hi".into()), delete: None };
        let ev = Event::RemoteOp { doc_id: "d".into(), op };
        let s = serde_json::to_string(&ev).unwrap();
        let val: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(val["event"], "remote_op");
        assert_eq!(val["retain"], 3);
        assert_eq!(val["insert"], "hi");
        assert!(val.get("delete").is_none());
    }

    #[test]
    fn event_error_format() {
        let ev = Event::Error { msg: "something broke".into() };
        let s = serde_json::to_string(&ev).unwrap();
        let val: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(val["event"], "error");
        assert_eq!(val["msg"], "something broke");
    }

    #[test]
    fn event_peer_count_format() {
        let ev = Event::PeerCount { doc_id: "d", count: 3 };
        let s = serde_json::to_string(&ev).unwrap();
        let val: serde_json::Value = serde_json::from_str(&s).unwrap();
        assert_eq!(val["event"], "peer_count");
        assert_eq!(val["count"], 3);
    }
}
