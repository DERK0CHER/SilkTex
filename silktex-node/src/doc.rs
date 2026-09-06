use anyhow::Result;
use loro::{ExportMode, LoroDoc};

use crate::TextOp;

pub struct Document {
    doc: LoroDoc,
    /// How many local ops (Command::Op) have been folded into `doc`. This is
    /// the C side's op sequence number: it counts every op handed to
    /// `apply_op`, in order, so the two counters cannot drift apart.
    local_seq: u64,
}

impl Document {
    pub fn new() -> Self {
        Self { doc: LoroDoc::new(), local_seq: 0 }
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
        /* Count the op before validating it. The C side numbers every op it
         * sends, valid or not; skipping a rejected one here would leave that
         * op unacknowledged forever and the editor transforming past it. */
        self.local_seq += 1;
        let vv = self.doc.oplog_vv();
        let t = self.doc.get_text("t");
        /* Validate before touching the doc: Loro's own bound check computes
         * `pos + len`, which wraps on a huge `delete` in release builds, and
         * a failed second step would leave a half-applied op. All units are
         * unicode code points (GTK offsets, LoroText::insert/delete, diff()). */
        let len = t.len_unicode();
        let del = op.delete.unwrap_or(0);
        let end = op
            .retain
            .checked_add(del)
            .ok_or_else(|| anyhow::anyhow!("op out of range: retain {} + delete {del}", op.retain))?;
        if end > len {
            anyhow::bail!("op out of range: retain {} + delete {del} > len {len}", op.retain);
        }
        /* Delete first, then insert at the same position — the order the C side
         * uses in apply_remote_op() and the meaning of the op produced by diff(). */
        if del > 0 {
            t.delete(op.retain, del)?;
        }
        if let Some(ins) = &op.insert {
            t.insert(op.retain, ins)?;
        }
        self.doc.commit();
        Ok(self.doc.export(ExportMode::updates(&vv))?)
    }

    /// Apply a remote Loro update; return the minimal op to replay in the UI
    /// together with the local-op count the diff was computed against.
    ///
    /// Both values are read under the caller's `&mut self`, so the count and
    /// the diff describe the very same document state — the editor needs that
    /// pairing to know which of its own in-flight ops the offsets predate.
    pub fn apply_update(&mut self, data: &[u8]) -> Result<(Option<TextOp>, u64)> {
        let before = self.get_content();
        self.doc.import(data)?;
        let after = self.get_content();
        if before == after {
            return Ok((None, self.local_seq));
        }
        Ok((Some(diff(&before, &after)), self.local_seq))
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
        let (result, base_seq) = doc2.apply_update(&update).unwrap();
        assert_eq!(doc2.get_content(), "hello");
        assert!(result.is_some());
        assert_eq!(base_seq, 0, "doc2 applied no local ops of its own");
    }

    #[test]
    fn apply_update_reports_local_op_count() {
        let mut doc1 = Document::new();
        let mut doc2 = Document::new();
        // doc2 makes two local edits of its own before the remote update lands.
        doc2.apply_op(&TextOp { retain: 0, insert: Some("ab".into()), delete: None }).unwrap();
        doc2.apply_op(&TextOp { retain: 2, insert: Some("cd".into()), delete: None }).unwrap();
        let update = doc1
            .apply_op(&TextOp { retain: 0, insert: Some("X".into()), delete: None })
            .unwrap();
        let (_, base_seq) = doc2.apply_update(&update).unwrap();
        assert_eq!(base_seq, 2);
    }

    #[test]
    fn rejected_op_still_counts() {
        let mut doc = Document::new();
        // Out of range: rejected, but the C side already numbered it.
        assert!(doc.apply_op(&TextOp { retain: 99, insert: Some("x".into()), delete: None }).is_err());
        let mut peer = Document::new();
        let update = peer
            .apply_op(&TextOp { retain: 0, insert: Some("hi".into()), delete: None })
            .unwrap();
        let (_, base_seq) = doc.apply_update(&update).unwrap();
        assert_eq!(base_seq, 1);
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
