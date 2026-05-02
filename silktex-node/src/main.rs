mod doc;
mod net;

use anyhow::Result;
use doc::Document;
use net::Network;
use serde::{Deserialize, Serialize};
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
    SessionReady { doc_id: &'a str, session_id: &'a str },
    RemoteOp     { doc_id: String,  #[serde(flatten)] op: TextOp },
    Snapshot     { doc_id: &'a str, content: String },
    PeerCount    { doc_id: &'a str, count: usize },
    Error        { msg: String },
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
