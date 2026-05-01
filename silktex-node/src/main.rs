mod doc;
mod net;

use anyhow::Result;
use doc::Document;
use net::Network;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::sync::{Mutex, mpsc};

/* ------------------------------------------------------------------ */
/* IPC protocol types                                                   */
/* ------------------------------------------------------------------ */

#[derive(Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
enum Command {
    CreateSession { doc_id: String, content: String },
    JoinSession   { doc_id: String, session_id: String },
    Op            { doc_id: String, retain: usize, insert: Option<String>, delete: Option<usize> },
    Shutdown,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
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

fn emit(ev: &Event<'_>) {
    match serde_json::to_string(ev) {
        Ok(s) => println!("{s}"),
        Err(e) => eprintln!("emit error: {e}"),
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

    /* Channel for incoming Loro updates from the network layer.
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
                    u64::from_be_bytes(update[..8].try_into().unwrap()) as usize
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

    loop {
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
                    emit(&Event::PeerCount { doc_id: &current_doc_id, count: op.retain });
                } else {
                    emit(&Event::RemoteOp { doc_id, op });
                }
            }

            /* Read the next command from the C side. */
            Ok(Some(line)) = lines.next_line() => {
                let line = line.trim().to_owned();
                if line.is_empty() { continue; }

                let cmd: Command = match serde_json::from_str(&line) {
                    Ok(c) => c,
                    Err(e) => {
                        emit(&Event::Error { msg: format!("parse: {e}") });
                        continue;
                    }
                };

                match cmd {
                    Command::CreateSession { doc_id, content } => {
                        doc.lock().await.set_content(&content);
                        current_doc_id = doc_id.clone();

                        let net = Network::start(
                            doc_id.clone(), net_tx.clone(), doc_id.clone(),
                        ).await?;
                        /* Use the doc_id as the session_id for simplicity;
                         * peers join by sharing this string. */
                        emit(&Event::SessionReady {
                            doc_id: &doc_id,
                            session_id: &doc_id,
                        });
                        network = Some(net);
                    }

                    Command::JoinSession { doc_id, session_id } => {
                        current_doc_id = doc_id.clone();

                        let (net, snapshot) = Network::join(
                            session_id.clone(), net_tx.clone(), doc_id.clone(),
                        ).await?;

                        if let Some(snap) = snapshot {
                            let mut d = doc.lock().await;
                            if let Err(e) = d.apply_snapshot(&snap) {
                                emit(&Event::Error { msg: format!("snapshot: {e}") });
                            } else {
                                let content = d.get_content();
                                drop(d);
                                emit(&Event::Snapshot { doc_id: &doc_id, content });
                            }
                        }
                        network = Some(net);
                    }

                    Command::Op { doc_id, retain, insert, delete } => {
                        let op = TextOp { retain, insert, delete };
                        match doc.lock().await.apply_op(&op) {
                            Ok(update) => {
                                if let Some(net) = &network {
                                    net.broadcast_op(update).await;
                                }
                            }
                            Err(e) => emit(&Event::Error { msg: e.to_string() }),
                        }
                        let _ = doc_id;
                    }

                    Command::Shutdown => break,
                }
            }

            else => break,
        }
    }

    Ok(())
}
