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

    pub fn set_content(&mut self, text: &str) -> Result<()> {
        let t = self.doc.get_text("t");
        let len = t.len_unicode();
        if len > 0 {
            t.delete(0, len)?;
        }
        if !text.is_empty() {
            t.insert(0, text)?;
        }
        self.doc.commit();
        Ok(())
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

#[cfg(test)]
mod tests {
    use super::{diff, Document};
    use crate::TextOp;

    // ---- Document basic ops --------------------------------------------

    #[test]
    fn new_doc_is_empty() {
        assert_eq!(Document::new().get_content(), "");
    }

    #[test]
    fn set_content_stores_text() {
        let mut doc = Document::new();
        doc.set_content("hello").unwrap();
        assert_eq!(doc.get_content(), "hello");
    }

    #[test]
    fn set_content_overwrites_previous() {
        let mut doc = Document::new();
        doc.set_content("first").unwrap();
        doc.set_content("second").unwrap();
        assert_eq!(doc.get_content(), "second");
    }

    #[test]
    fn set_content_to_empty_clears() {
        let mut doc = Document::new();
        doc.set_content("hello").unwrap();
        doc.set_content("").unwrap();
        assert_eq!(doc.get_content(), "");
    }

    #[test]
    fn apply_op_insert_appends() {
        let mut doc = Document::new();
        doc.set_content("hello").unwrap();
        let op = TextOp { retain: 5, insert: Some(" world".into()), delete: None };
        doc.apply_op(&op).unwrap();
        assert_eq!(doc.get_content(), "hello world");
    }

    #[test]
    fn apply_op_delete_removes_chars() {
        let mut doc = Document::new();
        doc.set_content("hello world").unwrap();
        let op = TextOp { retain: 5, insert: None, delete: Some(6) };
        doc.apply_op(&op).unwrap();
        assert_eq!(doc.get_content(), "hello");
    }

    #[test]
    fn apply_op_returns_nonempty_update_bytes() {
        let mut doc = Document::new();
        let op = TextOp { retain: 0, insert: Some("hi".into()), delete: None };
        let bytes = doc.apply_op(&op).unwrap();
        assert!(!bytes.is_empty());
    }

    #[test]
    fn apply_update_syncs_to_peer() {
        let mut doc1 = Document::new();
        let mut doc2 = Document::new();
        // Both start empty; apply op to doc1, then propagate update to doc2.
        let op = TextOp { retain: 0, insert: Some("hello".into()), delete: None };
        let update = doc1.apply_op(&op).unwrap();
        let result = doc2.apply_update(&update).unwrap();
        assert_eq!(doc2.get_content(), "hello");
        assert!(result.is_some());
    }

    #[test]
    fn snapshot_roundtrip() {
        let mut doc1 = Document::new();
        doc1.set_content("snapshot content").unwrap();
        let snap = doc1.export_snapshot();
        assert!(!snap.is_empty());
        let mut doc2 = Document::new();
        doc2.apply_snapshot(&snap).unwrap();
        assert_eq!(doc2.get_content(), "snapshot content");
    }

    // ---- diff() --------------------------------------------------------

    #[test]
    fn diff_insert_at_end() {
        let op = diff("hello", "hello world");
        assert_eq!(op.retain, 5);
        assert_eq!(op.insert.as_deref(), Some(" world"));
        assert_eq!(op.delete, None);
    }

    #[test]
    fn diff_delete_from_end() {
        let op = diff("hello world", "hello");
        assert_eq!(op.retain, 5);
        assert_eq!(op.insert, None);
        assert_eq!(op.delete, Some(6));
    }

    #[test]
    fn diff_empty_to_nonempty() {
        let op = diff("", "hello");
        assert_eq!(op.retain, 0);
        assert_eq!(op.insert.as_deref(), Some("hello"));
        assert_eq!(op.delete, None);
    }

    #[test]
    fn diff_nonempty_to_empty() {
        let op = diff("hello", "");
        assert_eq!(op.retain, 0);
        assert_eq!(op.insert, None);
        assert_eq!(op.delete, Some(5));
    }

    #[test]
    fn diff_identical_strings_is_noop() {
        let op = diff("hello", "hello");
        assert_eq!(op.retain, 5);
        assert_eq!(op.insert, None);
        assert_eq!(op.delete, None);
    }

    #[test]
    fn diff_middle_replacement() {
        let op = diff("abc", "axc");
        assert_eq!(op.retain, 1);
        assert_eq!(op.insert.as_deref(), Some("x"));
        assert_eq!(op.delete, Some(1));
    }

    #[test]
    fn diff_unicode_counts_chars_not_bytes() {
        // 'é' is 2 UTF-8 bytes but 1 char; retain should count chars.
        let op = diff("héllo", "héllo!");
        assert_eq!(op.retain, 5);
        assert_eq!(op.insert.as_deref(), Some("!"));
        assert_eq!(op.delete, None);
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
