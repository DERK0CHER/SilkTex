use anyhow::Result;
use loro::{ExportMode, LoroDoc};

use crate::TextOp;

pub struct Document {
    doc: LoroDoc,
}

impl Document {
    pub fn new() -> Self {
        Self { doc: LoroDoc::new() }
    }

    pub fn set_content(&mut self, text: &str) {
        let t = self.doc.get_text("t");
        let len = t.len_unicode();
        if len > 0 {
            t.delete(0, len).unwrap();
        }
        if !text.is_empty() {
            t.insert(0, text).unwrap();
        }
        self.doc.commit();
    }

    pub fn get_content(&self) -> String {
        self.doc.get_text("t").to_string()
    }

    /// Apply a local op, return the Loro update bytes to broadcast.
    pub fn apply_op(&mut self, op: &TextOp) -> Result<Vec<u8>> {
        let vv = self.doc.oplog_vv();
        let t = self.doc.get_text("t");
        if let Some(ins) = &op.insert {
            t.insert(op.retain, ins)?;
        }
        if let Some(del) = op.delete {
            if del > 0 {
                t.delete(op.retain, del)?;
            }
        }
        self.doc.commit();
        Ok(self.doc.export(ExportMode::updates(&vv))?)
    }

    /// Apply a remote Loro update; return the minimal op to replay in the UI.
    pub fn apply_update(&mut self, data: &[u8]) -> Result<Option<TextOp>> {
        let before = self.get_content();
        self.doc.import(data)?;
        let after = self.get_content();
        if before == after {
            return Ok(None);
        }
        Ok(Some(diff(&before, &after)))
    }

    pub fn apply_snapshot(&mut self, data: &[u8]) -> Result<()> {
        self.doc.import(data)?;
        Ok(())
    }

    pub fn export_snapshot(&self) -> Vec<u8> {
        self.doc.export(ExportMode::Snapshot).unwrap_or_default()
    }
}

/// Compute the minimal retain/insert/delete op that transforms `before` into `after`.
fn diff(before: &str, after: &str) -> TextOp {
    let bc: Vec<char> = before.chars().collect();
    let ac: Vec<char> = after.chars().collect();

    let prefix = bc.iter().zip(ac.iter()).take_while(|(a, b)| a == b).count();

    let max_suffix = bc.len().min(ac.len()) - prefix;
    let suffix = bc[prefix..]
        .iter()
        .rev()
        .zip(ac[prefix..].iter().rev())
        .take(max_suffix)
        .take_while(|(a, b)| a == b)
        .count();

    let del = bc.len() - prefix - suffix;
    let ins: String = ac[prefix..ac.len() - suffix].iter().collect();

    TextOp {
        retain: prefix,
        insert: if ins.is_empty() { None } else { Some(ins) },
        delete: if del == 0 { None } else { Some(del) },
    }
}
