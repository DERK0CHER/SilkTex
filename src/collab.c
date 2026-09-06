/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Collaboration module — thin C bridge to silktex-node (Rust sidecar).
 *
 * Protocol: newline-delimited JSON over the node's stdin/stdout.
 *
 *   C → node
 *     {"cmd":"create_session","doc_id":"<uuid>","content":"<tex>"}
 *     {"cmd":"join_session","doc_id":"<uuid>","session_id":"<id>"}
 *     {"cmd":"op","doc_id":"<uuid>","retain":<n>,"insert":"<s>"}
 *     {"cmd":"op","doc_id":"<uuid>","retain":<n>,"delete":<n>}
 *     {"cmd":"set_name","name":"<display name>"}
 *     {"cmd":"cursor","doc_id":"<uuid>","offset":<n>}
 *     {"cmd":"shutdown"}
 *
 *   node → C
 *     {"event":"session_ready","doc_id":"...","session_id":"<id>"}
 *     {"event":"remote_op","doc_id":"...","retain":<n>,"insert":"<s>","base_seq":<n>}
 *     {"event":"remote_op","doc_id":"...","retain":<n>,"delete":<n>,"base_seq":<n>}
 *     {"event":"snapshot","doc_id":"...","content":"<tex>"}
 *     {"event":"peer_count","doc_id":"...","count":<n>}
 *     {"event":"remote_cursor","doc_id":"...","peer_id":"...","offset":<n>}
 *     {"event":"peer_name","peer_id":"...","name":"<display name>"}
 *     {"event":"error","msg":"<text>"}
 *
 * Convergence: every op we send carries an implicit sequence number (1, 2, …)
 * and stays queued in C.pending until the node confirms it folded the op into
 * its document. That confirmation is "base_seq" on a remote op: the number of
 * our own ops the node had already applied when it computed that op's offsets.
 * Anything still queued is an edit the remote offsets do not know about, so a
 * remote op is transformed past each remaining queue entry before it touches
 * the buffer (see silktex_collab_transform_op).
 */

#include "collab.h"
#include "window-private.h"
#include "i18n.h"
#include <gio/gio.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <gtksourceview/gtksource.h>
#include <adwaita.h>
#include <string.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

/* ------------------------------------------------------------------ */
/* Peer cursor color palette                                           */
/* ------------------------------------------------------------------ */

#define PEER_COLORS_N 8
static const char *PEER_COLORS[PEER_COLORS_N] = {
    "#3584e4",  /* GNOME Blue   */
    "#e66100",  /* GNOME Orange */
    "#2ec27e",  /* GNOME Green  */
    "#9141ac",  /* GNOME Purple */
    "#c64600",  /* GNOME Brown  */
    "#00a0c4",  /* GNOME Teal   */
    "#f5c211",  /* GNOME Yellow */
    "#ed333b",  /* GNOME Red    */
};

/* ------------------------------------------------------------------ */
/* Per-peer state                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    char        *peer_id;
    char        *name;          /* display name, or NULL if unknown */
    int          color_idx;     /* index into PEER_COLORS           */
    GtkTextTag  *tag;           /* background highlight tag in C.buf (not owned) */
    int          last_offset;   /* last cursor offset, -1 if none   */
} PeerState;

static void peer_state_free(gpointer data)
{
    PeerState *ps = data;
    /* tag is owned by the buffer's tag table; don't unref it here */
    g_free(ps->peer_id);
    g_free(ps->name);
    g_free(ps);
}

/* ------------------------------------------------------------------ */
/* Global state (one node per process)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    GSubprocess      *proc;
    GDataInputStream *reader;
    GOutputStream    *writer;
    GCancellable     *cancel;

    SilktexEditor    *editor;          /* weak — cleared on tab close       */
    GtkTextBuffer    *buf;             /* weak — buffer with connected sigs */
    gulong            insert_handler;
    gulong            delete_handler;
    gulong            cursor_handler;  /* notify::cursor-position           */
    char             *doc_id;
    char             *session_id;

    gboolean          in_session;
    gboolean          session_pending;
    gboolean          hosting;         /* TRUE if we created the session    */
    gboolean          applying;        /* TRUE while writing a remote op    */
    int               peer_count;

    GHashTable       *peers;           /* peer_id → PeerState*              */
    int               next_color_idx;  /* round-robin color assignment      */
    guint             cursor_debounce; /* GSource ID for cursor broadcast   */

    guint64           local_seq;       /* ops sent so far this session      */
    GQueue           *pending;         /* PendingOp*, oldest first          */

    /* Window the collab UI lives in (weak — owns the tab view we annotate). */
    SilktexWindow *window;

    /* Header-bar button UI */
    GtkStack    *icon_stack;
    GtkSpinner  *btn_spinner;
    GtkLabel    *peers_badge;

    /* Popover UI */
    GtkEntry    *name_entry;           /* "Your name" field                 */
    GtkLabel    *status_label;
    GtkSpinner  *status_spinner;
    GtkRevealer *id_revealer;
    GtkEntry    *session_id_entry;
    GtkRevealer *join_revealer;
    GtkEditable *join_entry;
    GtkRevealer *leave_revealer;
    GtkButton   *primary_btn;
} Collab;

static Collab C; /* zero-initialized */

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void schedule_read(void);
static void collab_update_ui(void);
static void collab_send_name(void);
static void collab_update_tab_indicator(gboolean active);

/* ------------------------------------------------------------------ */
/* Tab indicator: lock icon on the editor page bound to the session    */
/* ------------------------------------------------------------------ */

static void collab_update_tab_indicator(gboolean active)
{
    if (!C.window || !C.window->tab_view) return;
    AdwTabView *tv = C.window->tab_view;
    guint n = adw_tab_view_get_n_pages(tv);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(tv, i);
        SilktexEditor *ed = silktex_window_editor_for_page(page);
        gboolean is_bound = active && ed && ed == C.editor;
        if (is_bound) {
            g_autoptr(GIcon) icon =
                g_themed_icon_new("system-users-symbolic");
            adw_tab_page_set_indicator_icon(page, icon);
            adw_tab_page_set_indicator_tooltip(page,
                _("This document is in a collaboration session"));
        } else if (adw_tab_page_get_indicator_icon(page)) {
            adw_tab_page_set_indicator_icon(page, NULL);
            adw_tab_page_set_indicator_tooltip(page, NULL);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Peer state helpers                                                  */
/* ------------------------------------------------------------------ */

static PeerState *get_or_create_peer(const char *peer_id)
{
    if (!C.peers)
        C.peers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, peer_state_free);

    PeerState *ps = g_hash_table_lookup(C.peers, peer_id);
    if (!ps) {
        ps             = g_new0(PeerState, 1);
        ps->peer_id    = g_strdup(peer_id);
        ps->color_idx  = C.next_color_idx++ % PEER_COLORS_N;
        ps->last_offset = -1;
        g_hash_table_insert(C.peers, g_strdup(peer_id), ps);
    }
    return ps;
}

static void ensure_peer_tag(PeerState *ps)
{
    if (ps->tag || !C.buf) return;

    g_autofree char *tag_name = g_strdup_printf("collab-cursor-%s", ps->peer_id);
    GtkTextTagTable *tbl = gtk_text_buffer_get_tag_table(C.buf);
    ps->tag = gtk_text_tag_table_lookup(tbl, tag_name);
    if (!ps->tag)
        ps->tag = gtk_text_buffer_create_tag(C.buf, tag_name,
                      "background", PEER_COLORS[ps->color_idx],
                      NULL);
}

static void update_peer_cursor(PeerState *ps, int offset)
{
    if (!C.buf || offset < 0) return;

    ensure_peer_tag(ps);
    if (!ps->tag) return;

    /* Remove highlight from the previous cursor position. */
    if (ps->last_offset >= 0) {
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_offset(C.buf, &s, ps->last_offset);
        e = s;
        if (!gtk_text_iter_is_end(&e)) gtk_text_iter_forward_char(&e);
        gtk_text_buffer_remove_tag(C.buf, ps->tag, &s, &e);
    }

    ps->last_offset = offset;

    /* Apply highlight to the character at the new cursor position. */
    GtkTextIter s, e;
    gtk_text_buffer_get_iter_at_offset(C.buf, &s, offset);
    e = s;
    if (!gtk_text_iter_is_end(&e)) {
        gtk_text_iter_forward_char(&e);
        gtk_text_buffer_apply_tag(C.buf, ps->tag, &s, &e);
    }
}

/* Remove all peer cursor highlights and reset peer table. */
static void clear_all_peer_cursors(void)
{
    if (!C.peers) return;

    if (C.buf) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(C.buf, &start, &end);
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, C.peers);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            PeerState *ps = v;
            if (ps->tag)
                gtk_text_buffer_remove_tag(C.buf, ps->tag, &start, &end);
        }
    }
    g_hash_table_remove_all(C.peers);
    C.next_color_idx = 0;
}

/* Invalidate tag pointers when the active editor (and thus buffer) changes. */
static void invalidate_peer_tags(void)
{
    if (!C.peers) return;
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, C.peers);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        PeerState *ps = v;
        ps->tag         = NULL;
        ps->last_offset = -1;
    }
}

/* ------------------------------------------------------------------ */
/* Unacknowledged local ops + operational transform                    */
/* ------------------------------------------------------------------ */

/* Offsets are character counts in a text buffer, so they are nowhere near
 * G_MAXINT; clamping to a quarter of it keeps every intermediate sum below
 * the overflow point without ever touching a realistic value. */
#define COLLAB_OFFSET_MAX (G_MAXINT / 4)

/* A healthy node acknowledges through base_seq on every remote op, so the
 * queue stays a handful of entries deep. The cap only bites if the node
 * wedges: we then drop the oldest entries rather than grow without bound. */
#define COLLAB_PENDING_MAX 4096

typedef struct {
    guint64 seq;
    int     retain;
    int     insert_len;  /* characters, not bytes */
    int     delete_len;
} PendingOp;

static int collab_clamp_offset(int v)
{
    if (v < 0)                 return 0;
    if (v > COLLAB_OFFSET_MAX) return COLLAB_OFFSET_MAX;
    return v;
}

gboolean silktex_collab_transform_op(int *r_retain, int *r_delete,
                                     int l_retain, int l_insert_len, int l_delete_len)
{
    if (!r_retain || !r_delete) return TRUE;

    int rr = collab_clamp_offset(*r_retain);
    int rd = collab_clamp_offset(*r_delete);
    int lr = collab_clamp_offset(l_retain);
    int li = collab_clamp_offset(l_insert_len);
    int ld = collab_clamp_offset(l_delete_len);

    int l_lo = lr, l_hi = lr + ld;

    /* Conflict policy — the delete wins over a concurrent insert that lands
     * strictly inside it. Two halves of one rule, applied consistently:
     *   · a remote insertion point strictly inside the local deletion is
     *     dropped (this return value), and
     *   · a local insertion strictly inside the remote deletion is swallowed
     *     by growing that deletion (see the `rd += li` branch below).
     * Boundaries are never "inside": an insert exactly at the start of a
     * deletion lands before it, one exactly at its end lands after it, and
     * both survive. */
    gboolean keep_insert = !(ld > 0 && l_lo < rr && rr < l_hi);

    /* ── local delete ── */
    int before = MIN(l_hi, rr) - l_lo;          /* removed before the remote start */
    if (before < 0) before = 0;

    int ov_lo   = MAX(rr, l_lo);                /* removed inside the remote range */
    int ov_hi   = MIN(rr + rd, l_hi);
    int overlap = ov_hi - ov_lo;
    if (overlap < 0) overlap = 0;

    rr -= before;                               /* start clamps to the local start */
    rd -= overlap;
    if (rr < 0) rr = 0;
    if (rd < 0) rd = 0;

    /* ── local insert ──
     * A local op is always a pure insert or a pure delete (the two
     * GtkTextBuffer signals), so when li > 0 the block above was a no-op and
     * l_lo and rr are still offsets into the same, shared document state. */
    if (li > 0) {
        if (l_lo <= rr) {
            /* Tie at equal offsets: the local text goes first, so the remote
             * position steps over it. The matching rule on the other side is
             * that a local insert at an equal offset does not move — that is
             * what makes the two orderings agree. */
            rr += li;
        } else if (l_lo < rr + rd) {
            rd += li;
        }
        /* l_lo >= rr + rd: entirely after the remote range — no change. */
    }

    *r_retain = collab_clamp_offset(rr);
    *r_delete = collab_clamp_offset(rd);
    return keep_insert;
}

/* Forget everything we sent: called whenever the session (and with it the
 * node's counter) restarts. */
static void collab_reset_pending(void)
{
    if (C.pending) {
        g_queue_free_full(C.pending, g_free);
        C.pending = NULL;
    }
    C.local_seq = 0;
}

static void collab_queue_local_op(int retain, int insert_len, int delete_len)
{
    if (!C.pending) C.pending = g_queue_new();

    PendingOp *p   = g_new0(PendingOp, 1);
    p->seq         = C.local_seq;
    p->retain      = retain;
    p->insert_len  = insert_len;
    p->delete_len  = delete_len;
    g_queue_push_tail(C.pending, p);

    while (C.pending->length > COLLAB_PENDING_MAX) {
        g_warning_once("collab: node is not acknowledging ops; dropping the "
                       "oldest unacknowledged edits");
        g_free(g_queue_pop_head(C.pending));
    }
}

/* Drop the ops the node has already folded into the state it diffed against. */
static void collab_ack_through(guint64 base_seq)
{
    if (!C.pending) return;
    while (C.pending->head) {
        PendingOp *p = C.pending->head->data;
        if (p->seq > base_seq) break;
        g_free(g_queue_pop_head(C.pending));
    }
}

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static void collab_send(const char *json)
{
    if (!C.writer) return;
    GError *err = NULL;
    g_autofree char *line = g_strconcat(json, "\n", NULL);
    g_output_stream_write_all(C.writer, line, strlen(line), NULL, NULL, &err);
    if (err) {
        g_warning("collab: send error: %s", err->message);
        g_clear_error(&err);
    }
}

/* TRUE while local edits must be mirrored to the node: during a session, and
 * while hosting one that is still starting — the node already holds our
 * content, so edits typed before session_ready would otherwise desync it. */
static gboolean collab_mirroring(void)
{
    return C.in_session || (C.session_pending && C.hosting);
}

static void collab_send_op(int retain, const char *insert, int delete_count)
{
    if (!collab_mirroring() || !C.doc_id) return;

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");   json_builder_add_string_value(b, "op");
    json_builder_set_member_name(b, "doc_id"); json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "retain"); json_builder_add_int_value(b, retain);
    if (insert) {
        json_builder_set_member_name(b, "insert");
        json_builder_add_string_value(b, insert);
    }
    if (delete_count > 0) {
        json_builder_set_member_name(b, "delete");
        json_builder_add_int_value(b, delete_count);
    }
    json_builder_end_object(b);

    JsonGenerator *gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);

    /* The node numbers the ops it receives in the same order; remember this
     * one until a remote op's base_seq says it has been folded in. */
    C.local_seq++;
    collab_queue_local_op(retain,
                          insert ? (int)g_utf8_strlen(insert, -1) : 0,
                          delete_count > 0 ? delete_count : 0);
}

static void collab_send_name(void)
{
    const char *name = C.name_entry
        ? gtk_editable_get_text(GTK_EDITABLE(C.name_entry))
        : NULL;
    if (!name || !*name) name = g_get_user_name();

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");  json_builder_add_string_value(b, "set_name");
    json_builder_set_member_name(b, "name"); json_builder_add_string_value(b, name);
    json_builder_end_object(b);

    JsonGenerator *gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);
}

static gboolean send_cursor_debounced(gpointer ud)
{
    (void)ud;
    C.cursor_debounce = 0;
    if (!C.in_session || !C.buf || !C.doc_id) return G_SOURCE_REMOVE;

    GtkTextMark *mark = gtk_text_buffer_get_insert(C.buf);
    GtkTextIter  iter;
    gtk_text_buffer_get_iter_at_mark(C.buf, &iter, mark);
    int offset = gtk_text_iter_get_offset(&iter);

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");    json_builder_add_string_value(b, "cursor");
    json_builder_set_member_name(b, "doc_id"); json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "offset"); json_builder_add_int_value(b, offset);
    json_builder_end_object(b);

    JsonGenerator *gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/* Applying remote ops to the local buffer                             */
/* ------------------------------------------------------------------ */

static void apply_remote_op(int retain, const char *insert, int delete_count)
{
    if (!C.editor || retain < 0) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(C.editor);
    GtkTextBuffer   *buf  = GTK_TEXT_BUFFER(sbuf);

    /* Last safety net: a transformed op must never address past the buffer. */
    int n_chars = gtk_text_buffer_get_char_count(buf);
    if (retain > n_chars) retain = n_chars;
    if (delete_count < 0) delete_count = 0;
    if (delete_count > n_chars - retain) delete_count = n_chars - retain;

    C.applying = TRUE;
    gtk_text_buffer_begin_irreversible_action(buf);

    GtkTextIter it;
    gtk_text_buffer_get_iter_at_offset(buf, &it, retain);

    if (delete_count > 0) {
        GtkTextIter end = it;
        gtk_text_iter_forward_chars(&end, delete_count);
        gtk_text_buffer_delete(buf, &it, &end);
        gtk_text_buffer_get_iter_at_offset(buf, &it, retain);
    }
    if (insert && *insert) gtk_text_buffer_insert(buf, &it, insert, -1);

    gtk_text_buffer_end_irreversible_action(buf);
    C.applying = FALSE;
}

/* ------------------------------------------------------------------ */
/* Reading events from the node                                        */
/* ------------------------------------------------------------------ */

static void on_node_line(GObject *src, GAsyncResult *res, gpointer ud)
{
    GError *err  = NULL;
    gsize   len  = 0;
    char   *line =
        g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(src), res, &len, &err);

    if (!line) {
        if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            /* do_leave_session already tore the node down and reset state
             * (and the window may be gone by now) — nothing to touch. */
            g_clear_error(&err);
            return;
        }
        if (err) g_warning("collab: node stream closed: %s", err->message);
        /* The node exited on its own: drop our handles so the next session
         * respawns it instead of writing into a dead pipe. */
        g_clear_object(&C.cancel);
        g_clear_object(&C.reader);
        g_clear_object(&C.proc);
        C.writer          = NULL;
        C.session_pending = FALSE;
        C.in_session      = FALSE;
        C.peer_count      = 0;
        collab_reset_pending();
        clear_all_peer_cursors();
        collab_update_ui();
        g_clear_error(&err);
        return;
    }

    JsonParser *p = json_parser_new();
    if (!json_parser_load_from_data(p, line, (gssize)len, NULL)) goto done;

    JsonNode *root = json_parser_get_root(p);
    if (!JSON_NODE_HOLDS_OBJECT(root)) goto done;

    JsonObject *obj   = json_node_get_object(root);
    const char *event = json_object_has_member(obj, "event")
        ? json_object_get_string_member(obj, "event") : "";
    if (!event) goto done; /* non-string "event" member */

    if (g_str_equal(event, "remote_op")) {
        int         retain = json_object_has_member(obj, "retain")
                                 ? (int)json_object_get_int_member(obj, "retain") : 0;
        const char *ins    = json_object_has_member(obj, "insert")
                                 ? json_object_get_string_member(obj, "insert") : NULL;
        int         del    = json_object_has_member(obj, "delete")
                                 ? (int)json_object_get_int_member(obj, "delete") : 0;

        if (json_object_has_member(obj, "base_seq")) {
            gint64 base = json_object_get_int_member(obj, "base_seq");
            collab_ack_through(base < 0 ? 0 : (guint64)base);
        } else {
            /* Node predates base_seq: it cannot tell us what it has folded in,
             * so assume everything and fall back to the untransformed path. */
            g_warning_once("collab: silktex-node does not report base_seq; "
                           "concurrent edits may drift");
            collab_ack_through(G_MAXUINT64);
        }

        /* Whatever is still queued is an edit the node had not seen when it
         * computed these offsets — replay the remote op past each of them. */
        if (C.pending) {
            for (GList *l = C.pending->head; l; l = l->next) {
                PendingOp *p = l->data;
                if (!silktex_collab_transform_op(&retain, &del, p->retain,
                                                 p->insert_len, p->delete_len))
                    ins = NULL; /* insertion point was swallowed by a local delete */
            }
        }
        apply_remote_op(retain, ins, del);

    } else if (g_str_equal(event, "snapshot")) {
        const char *content = json_object_has_member(obj, "content")
                                  ? json_object_get_string_member(obj, "content") : NULL;
        if (content && C.editor) {
            GtkTextBuffer *buf = GTK_TEXT_BUFFER(silktex_editor_get_buffer(C.editor));
            C.applying = TRUE;
            /* Irreversible: an undo of the snapshot load would be echoed
             * to peers as a delete + insert of our pre-join content. */
            gtk_text_buffer_begin_irreversible_action(buf);
            silktex_editor_set_text(C.editor, content, -1);
            gtk_text_buffer_end_irreversible_action(buf);
            C.applying = FALSE;
        }

    } else if (g_str_equal(event, "session_ready")) {
        const char *sid = json_object_has_member(obj, "session_id")
                              ? json_object_get_string_member(obj, "session_id") : "?";
        C.session_pending = FALSE;
        C.in_session      = TRUE;
        g_free(C.session_id);
        C.session_id = g_strdup(sid);
        collab_update_ui();
        /* Announce our display name to peers now that the session is established. */
        collab_send_name();

    } else if (g_str_equal(event, "peer_count")) {
        int prev = C.peer_count;
        C.peer_count = json_object_has_member(obj, "count")
                           ? (int)json_object_get_int_member(obj, "count") : 0;
        /* Re-broadcast our display name whenever a new peer joins, so the
         * host (whose set_name was sent before any peer was listening)
         * still reaches late joiners. */
        if (C.in_session && C.peer_count > prev) collab_send_name();
        collab_update_ui();

    } else if (g_str_equal(event, "remote_cursor")) {
        const char *peer_id = json_object_has_member(obj, "peer_id")
                                  ? json_object_get_string_member(obj, "peer_id") : NULL;
        int offset = json_object_has_member(obj, "offset")
                         ? (int)json_object_get_int_member(obj, "offset") : 0;
        if (peer_id) {
            PeerState *ps = get_or_create_peer(peer_id);
            update_peer_cursor(ps, offset);
        }

    } else if (g_str_equal(event, "peer_name")) {
        const char *peer_id = json_object_has_member(obj, "peer_id")
                                  ? json_object_get_string_member(obj, "peer_id") : NULL;
        const char *name    = json_object_has_member(obj, "name")
                                  ? json_object_get_string_member(obj, "name") : NULL;
        if (peer_id && name) {
            PeerState *ps = get_or_create_peer(peer_id);
            g_free(ps->name);
            ps->name = g_strdup(name);
            collab_update_ui();
        }

    } else if (g_str_equal(event, "error")) {
        const char *msg = json_object_has_member(obj, "msg")
                              ? json_object_get_string_member(obj, "msg") : "?";
        g_warning("silktex-node: %s", msg);
        C.session_pending = FALSE;
        collab_update_ui();
    }

done:
    g_object_unref(p);
    g_free(line);
    schedule_read();
}

static void schedule_read(void)
{
    if (!C.reader || !C.cancel) return;
    g_data_input_stream_read_line_async(C.reader, G_PRIORITY_DEFAULT, C.cancel,
                                        on_node_line, NULL);
}

/* ------------------------------------------------------------------ */
/* Buffer signal handlers                                              */
/* ------------------------------------------------------------------ */

static void on_insert_text(GtkTextBuffer *buf, GtkTextIter *loc, const char *text,
                           int byte_len, gpointer ud)
{
    (void)buf; (void)ud;
    if (C.applying || !collab_mirroring()) return;
    int retain = gtk_text_iter_get_offset(loc);
    g_autofree char *safe = g_strndup(text, (gsize)byte_len);
    collab_send_op(retain, safe, 0);
}

static void on_delete_range(GtkTextBuffer *buf, GtkTextIter *start, GtkTextIter *end,
                            gpointer ud)
{
    (void)buf; (void)ud;
    if (C.applying || !collab_mirroring()) return;
    int s   = gtk_text_iter_get_offset(start);
    int del = gtk_text_iter_get_offset(end) - s;
    if (del > 0) collab_send_op(s, NULL, del);
}

static void on_cursor_position_changed(GtkTextBuffer *buf, GParamSpec *pspec, gpointer ud)
{
    (void)buf; (void)pspec; (void)ud;
    if (!C.in_session || C.applying) return;
    if (C.cursor_debounce) {
        g_source_remove(C.cursor_debounce);
        C.cursor_debounce = 0;
    }
    C.cursor_debounce = g_timeout_add(80, send_cursor_debounced, NULL);
}

static void on_name_entry_changed(GtkEditable *entry, gpointer ud)
{
    (void)entry; (void)ud;
    if (C.in_session) collab_send_name();
}

/* ------------------------------------------------------------------ */
/* Node lifecycle                                                      */
/* ------------------------------------------------------------------ */

static char *find_node_binary(void)
{
#ifdef __APPLE__
    char     exe_buf[PATH_MAX];
    uint32_t size = PATH_MAX;
    if (_NSGetExecutablePath(exe_buf, &size) == 0) {
        g_autofree char *dir = g_path_get_dirname(exe_buf);
        g_autofree char *adj = g_build_filename(dir, "silktex-node", NULL);
        if (g_file_test(adj, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(adj);
    }
#else
    g_autofree char *self = g_file_read_link("/proc/self/exe", NULL);
    if (self) {
        g_autofree char *dir = g_path_get_dirname(self);
        g_autofree char *adj = g_build_filename(dir, "silktex-node", NULL);
        if (g_file_test(adj, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup(adj);
    }
#endif
    return g_find_program_in_path("silktex-node");
}

static gboolean collab_start_node(void)
{
    if (C.proc) return TRUE;

    g_autofree char *node = find_node_binary();
    if (!node) {
        g_message("collab: silktex-node not found; collaboration disabled");
        return FALSE;
    }

    GError    *err  = NULL;
    const char *argv[] = {node, NULL};
    GSubprocess *proc =
        g_subprocess_newv((const char *const *)argv,
                          G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                          &err);
    if (!proc) {
        g_warning("collab: failed to start silktex-node: %s", err->message);
        g_clear_error(&err);
        return FALSE;
    }

    C.proc   = proc;
    C.cancel = g_cancellable_new();
    C.writer = g_subprocess_get_stdin_pipe(proc);
    C.reader = g_data_input_stream_new(g_subprocess_get_stdout_pipe(proc));
    schedule_read();
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Session commands                                                    */
/* ------------------------------------------------------------------ */

static void do_create_session(void)
{
    if (C.in_session || C.session_pending) return;
    if (!C.editor || !collab_start_node()) return;

    C.session_pending = TRUE;
    C.hosting         = TRUE;
    C.peer_count      = 0;
    collab_reset_pending();
    clear_all_peer_cursors();
    collab_update_ui();

    g_free(C.doc_id);
    C.doc_id = g_uuid_string_random();

    g_autofree char *content = silktex_editor_get_text(C.editor);

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");     json_builder_add_string_value(b, "create_session");
    json_builder_set_member_name(b, "doc_id");  json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "content"); json_builder_add_string_value(b, content ? content : "");
    json_builder_end_object(b);

    JsonGenerator *gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);
}

static void do_join_session(const char *session_id)
{
    if (C.in_session || C.session_pending) return;
    if (!collab_start_node() || !session_id || !*session_id) return;

    C.session_pending = TRUE;
    C.hosting         = FALSE;
    C.peer_count      = 0;
    collab_reset_pending();
    clear_all_peer_cursors();
    collab_update_ui();

    g_free(C.doc_id);
    C.doc_id = g_uuid_string_random();

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");        json_builder_add_string_value(b, "join_session");
    json_builder_set_member_name(b, "doc_id");     json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "session_id"); json_builder_add_string_value(b, session_id);
    json_builder_end_object(b);

    JsonGenerator *gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);

    g_free(C.session_id);
    C.session_id = g_strdup(session_id);
    collab_update_ui();
}

static void do_leave_session(void)
{
    collab_send("{\"cmd\":\"shutdown\"}");

    if (C.cursor_debounce) {
        g_source_remove(C.cursor_debounce);
        C.cursor_debounce = 0;
    }

    g_cancellable_cancel(C.cancel);
    g_clear_object(&C.cancel);
    g_clear_object(&C.reader);
    g_clear_object(&C.proc);
    C.writer = NULL;

    collab_reset_pending();
    clear_all_peer_cursors();

    C.in_session      = FALSE;
    C.session_pending = FALSE;
    C.peer_count      = 0;
    g_clear_pointer(&C.doc_id,     g_free);
    g_clear_pointer(&C.session_id, g_free);
    collab_update_ui();
}

/* ------------------------------------------------------------------ */
/* Popover UI update                                                   */
/* ------------------------------------------------------------------ */

static void collab_update_ui(void)
{
    collab_update_tab_indicator(C.in_session || C.session_pending);

    if (!C.icon_stack) return;

    if (C.in_session) {
        gtk_stack_set_visible_child_name(C.icon_stack, "online");

        gboolean waiting = (C.peer_count == 0);

        if (C.btn_spinner) {
            gtk_widget_set_visible(GTK_WIDGET(C.btn_spinner), waiting);
            if (waiting) gtk_spinner_start(C.btn_spinner);
            else         gtk_spinner_stop(C.btn_spinner);
        }
        if (C.peers_badge) {
            if (C.peer_count > 0) {
                g_autofree char *badge = g_strdup_printf("%d", C.peer_count);
                gtk_label_set_text(C.peers_badge, badge);
                gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), TRUE);
            } else {
                gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), FALSE);
            }
        }

        /* Build status string, optionally listing known peer names. */
        g_autofree char *status = NULL;
        if (waiting) {
            status = g_strdup(_("Searching for peers on the network…"));
        } else {
            /* Collect known display names. */
            GString *names = g_string_new(NULL);
            if (C.peers) {
                GHashTableIter it;
                gpointer k, v;
                g_hash_table_iter_init(&it, C.peers);
                while (g_hash_table_iter_next(&it, &k, &v)) {
                    PeerState *ps = v;
                    if (ps->name) {
                        if (names->len) g_string_append(names, ", ");
                        g_string_append(names, ps->name);
                    }
                }
            }
            if (names->len) {
                status = g_strdup_printf(
                    C.peer_count == 1 ? _("%d peer: %s") : _("%d peers: %s"),
                    C.peer_count, names->str);
            } else {
                status = g_strdup_printf(
                    C.peer_count == 1 ? _("%d peer connected") : _("%d peers connected"),
                    C.peer_count);
            }
            g_string_free(names, TRUE);
        }
        if (C.status_label)  gtk_label_set_text(C.status_label, status);
        if (C.status_spinner) {
            gtk_widget_set_visible(GTK_WIDGET(C.status_spinner), waiting);
            if (waiting) gtk_spinner_start(C.status_spinner);
            else         gtk_spinner_stop(C.status_spinner);
        }

        if (C.session_id_entry)
            gtk_editable_set_text(GTK_EDITABLE(C.session_id_entry),
                                  C.session_id ? C.session_id : "");
        if (C.primary_btn) gtk_button_set_label(C.primary_btn, _("Leave Session"));

        gtk_revealer_set_reveal_child(C.id_revealer,    TRUE);
        gtk_revealer_set_reveal_child(C.join_revealer,  FALSE);
        gtk_revealer_set_reveal_child(C.leave_revealer, TRUE);

    } else if (C.session_pending) {
        gtk_stack_set_visible_child_name(C.icon_stack, "online");
        if (C.peers_badge) gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), FALSE);

        if (C.btn_spinner) {
            gtk_widget_set_visible(GTK_WIDGET(C.btn_spinner), TRUE);
            gtk_spinner_start(C.btn_spinner);
        }
        if (C.status_spinner) {
            gtk_widget_set_visible(GTK_WIDGET(C.status_spinner), TRUE);
            gtk_spinner_start(C.status_spinner);
        }
        if (C.status_label) gtk_label_set_text(C.status_label, _("Starting collaboration session…"));
        if (C.primary_btn)  gtk_button_set_label(C.primary_btn, _("Cancel"));

        gtk_revealer_set_reveal_child(C.id_revealer,    FALSE);
        gtk_revealer_set_reveal_child(C.join_revealer,  FALSE);
        gtk_revealer_set_reveal_child(C.leave_revealer, TRUE);

    } else {
        gtk_stack_set_visible_child_name(C.icon_stack, "offline");
        if (C.peers_badge) gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), FALSE);
        if (C.primary_btn) gtk_button_set_label(C.primary_btn, _("Leave Session"));

        if (C.btn_spinner) {
            gtk_spinner_stop(C.btn_spinner);
            gtk_widget_set_visible(GTK_WIDGET(C.btn_spinner), FALSE);
        }
        if (C.status_spinner) {
            gtk_spinner_stop(C.status_spinner);
            gtk_widget_set_visible(GTK_WIDGET(C.status_spinner), FALSE);
        }
        if (C.status_label) gtk_label_set_text(C.status_label, _("Not in a collaboration session"));

        gtk_revealer_set_reveal_child(C.id_revealer,    FALSE);
        gtk_revealer_set_reveal_child(C.join_revealer,  TRUE);
        gtk_revealer_set_reveal_child(C.leave_revealer, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* Popover button callbacks                                            */
/* ------------------------------------------------------------------ */

static void on_start_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn; (void)ud;
    do_create_session();
}

static void on_primary_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn; (void)ud;
    do_leave_session();
}

static void on_join_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn; (void)ud;
    if (!C.join_entry) return;
    g_autofree char *sid = g_strdup(gtk_editable_get_text(C.join_entry));
    if (sid) g_strstrip(sid); /* pasted codes often carry whitespace */
    if (sid && *sid) {
        do_join_session(sid);
        gtk_editable_set_text(C.join_entry, "");
    }
}

static void on_join_entry_activate(GtkEntry *entry, gpointer ud)
{
    (void)entry; (void)ud;
    on_join_btn_clicked(NULL, NULL);
}

static void on_copy_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn; (void)ud;
    if (!C.session_id_entry) return;
    const char *code = gtk_editable_get_text(GTK_EDITABLE(C.session_id_entry));
    if (!code || !*code) return;
    GdkClipboard *cb = gdk_display_get_clipboard(gdk_display_get_default());
    gdk_clipboard_set_text(cb, code);
}

/* ------------------------------------------------------------------ */
/* Public: silktex_collab_setup_window                                 */
/* ------------------------------------------------------------------ */

void silktex_collab_setup_window(SilktexWindow *self)
{
    if (!self->btn_collab) return;

    if (C.window)
        g_object_remove_weak_pointer(G_OBJECT(C.window), (gpointer *)&C.window);
    C.window = self;
    g_object_add_weak_pointer(G_OBJECT(self), (gpointer *)&C.window);

    /* ── Header button child: icon stack + spinner + peer-count badge ── */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 150);
    C.icon_stack = GTK_STACK(stack);

    GtkWidget *offline_img = gtk_image_new_from_icon_name("system-users-symbolic");
    gtk_widget_add_css_class(offline_img, "dim-label");
    gtk_stack_add_named(GTK_STACK(stack), offline_img, "offline");

    GtkWidget *online_img = gtk_image_new_from_icon_name("system-users-symbolic");
    gtk_widget_add_css_class(online_img, "collab-online-icon");
    gtk_stack_add_named(GTK_STACK(stack), online_img, "online");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "offline");

    GtkWidget *btn_spinner = gtk_spinner_new();
    gtk_widget_set_visible(btn_spinner, FALSE);
    C.btn_spinner = GTK_SPINNER(btn_spinner);

    GtkWidget *badge = gtk_label_new("");
    gtk_widget_add_css_class(badge, "collab-badge");
    gtk_widget_set_visible(badge, FALSE);
    C.peers_badge = GTK_LABEL(badge);

    gtk_box_append(GTK_BOX(btn_box), stack);
    gtk_box_append(GTK_BOX(btn_box), btn_spinner);
    gtk_box_append(GTK_BOX(btn_box), badge);
    gtk_menu_button_set_child(self->btn_collab, btn_box);

    /* ── Popover ── */
    GtkWidget *popover_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_maximum_size(ADW_CLAMP(clamp), 320);
    adw_clamp_set_child(ADW_CLAMP(clamp), popover_box);

    /* ── Your name row (always visible) ── */
    GtkWidget *name_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(name_row, 12);
    gtk_widget_set_margin_end(name_row, 12);
    gtk_widget_set_margin_top(name_row, 10);
    gtk_widget_set_margin_bottom(name_row, 6);

    GtkWidget *name_lbl = gtk_label_new(_("Your name"));
    gtk_widget_add_css_class(name_lbl, "caption");
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0f);

    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), _("Enter your name…"));
    gtk_widget_set_hexpand(name_entry, TRUE);
    /* Pre-fill with the system username. */
    gtk_editable_set_text(GTK_EDITABLE(name_entry), g_get_user_name());
    C.name_entry = GTK_ENTRY(name_entry);
    g_signal_connect(name_entry, "changed", G_CALLBACK(on_name_entry_changed), NULL);

    gtk_box_append(GTK_BOX(name_row), name_lbl);
    gtk_box_append(GTK_BOX(name_row), name_entry);
    gtk_box_append(GTK_BOX(popover_box), name_row);

    gtk_box_append(GTK_BOX(popover_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Status row: icon · label · spinner ── */
    GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(status_row, 12);
    gtk_widget_set_margin_end(status_row, 12);
    gtk_widget_set_margin_top(status_row, 10);
    gtk_widget_set_margin_bottom(status_row, 8);

    GtkWidget *status_icon = gtk_image_new_from_icon_name("system-users-symbolic");

    GtkWidget *status_lbl = gtk_label_new(_("Not in a collaboration session"));
    gtk_label_set_xalign(GTK_LABEL(status_lbl), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(status_lbl), TRUE);
    gtk_widget_set_hexpand(status_lbl, TRUE);
    gtk_widget_add_css_class(status_lbl, "heading");
    C.status_label = GTK_LABEL(status_lbl);

    GtkWidget *status_spinner = gtk_spinner_new();
    gtk_widget_set_visible(status_spinner, FALSE);
    C.status_spinner = GTK_SPINNER(status_spinner);

    gtk_box_append(GTK_BOX(status_row), status_icon);
    gtk_box_append(GTK_BOX(status_row), status_lbl);
    gtk_box_append(GTK_BOX(status_row), status_spinner);
    gtk_box_append(GTK_BOX(popover_box), status_row);

    gtk_box_append(GTK_BOX(popover_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Session ID row (shown when hosting) ── */
    GtkWidget *id_rev = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(id_rev), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(id_rev), FALSE);
    C.id_revealer = GTK_REVEALER(id_rev);

    GtkWidget *id_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(id_box, 12);
    gtk_widget_set_margin_end(id_box, 12);
    gtk_widget_set_margin_top(id_box, 8);
    gtk_widget_set_margin_bottom(id_box, 4);

    GtkWidget *id_title = gtk_label_new(_("Session Code"));
    gtk_widget_add_css_class(id_title, "caption-heading");
    gtk_label_set_xalign(GTK_LABEL(id_title), 0.0f);
    gtk_box_append(GTK_BOX(id_box), id_title);

    GtkWidget *code_row   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *code_entry = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(code_entry), FALSE);
    gtk_widget_add_css_class(code_entry, "monospace");
    gtk_widget_set_hexpand(code_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(code_entry), _("Starting session…"));
    C.session_id_entry = GTK_ENTRY(code_entry);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_add_css_class(copy_btn, "flat");
    gtk_widget_add_css_class(copy_btn, "circular");
    gtk_widget_set_tooltip_text(copy_btn, _("Copy session code"));
    gtk_widget_set_valign(copy_btn, GTK_ALIGN_CENTER);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_btn_clicked), NULL);

    gtk_box_append(GTK_BOX(code_row), code_entry);
    gtk_box_append(GTK_BOX(code_row), copy_btn);
    gtk_box_append(GTK_BOX(id_box), code_row);
    gtk_revealer_set_child(GTK_REVEALER(id_rev), id_box);
    gtk_box_append(GTK_BOX(popover_box), id_rev);

    /* ── Not-in-session section: entry + Start/Join buttons ── */
    GtkWidget *join_rev = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(join_rev),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(join_rev), TRUE);
    C.join_revealer = GTK_REVEALER(join_rev);

    GtkWidget *join_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(join_outer, 12);
    gtk_widget_set_margin_end(join_outer, 12);
    gtk_widget_set_margin_top(join_outer, 8);
    gtk_widget_set_margin_bottom(join_outer, 12);

    GtkWidget *join_title = gtk_label_new(_("Join a Session"));
    gtk_widget_add_css_class(join_title, "caption-heading");
    gtk_label_set_xalign(GTK_LABEL(join_title), 0.0f);
    gtk_box_append(GTK_BOX(join_outer), join_title);

    GtkWidget *join_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(join_entry), _("Paste session code"));
    gtk_entry_set_activates_default(GTK_ENTRY(join_entry), TRUE);
    gtk_widget_add_css_class(join_entry, "monospace");
    gtk_widget_set_hexpand(join_entry, TRUE);
    C.join_entry = GTK_EDITABLE(join_entry);
    g_signal_connect(join_entry, "activate", G_CALLBACK(on_join_entry_activate), NULL);
    gtk_box_append(GTK_BOX(join_outer), join_entry);

    GtkWidget *btns_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(btns_box, 2);

    GtkWidget *start_btn = gtk_button_new_with_label(_("Start Session"));
    gtk_widget_add_css_class(start_btn, "suggested-action");
    gtk_widget_add_css_class(start_btn, "pill");
    gtk_widget_set_hexpand(start_btn, TRUE);
    g_signal_connect(start_btn, "clicked", G_CALLBACK(on_start_btn_clicked), NULL);

    GtkWidget *join_do_btn = gtk_button_new_with_label(_("Join"));
    gtk_widget_add_css_class(join_do_btn, "pill");
    gtk_widget_set_hexpand(join_do_btn, TRUE);
    gtk_widget_set_tooltip_text(join_do_btn, _("Join the session whose ID is entered above"));
    g_signal_connect(join_do_btn, "clicked", G_CALLBACK(on_join_btn_clicked), NULL);

    gtk_box_append(GTK_BOX(btns_box), start_btn);
    gtk_box_append(GTK_BOX(btns_box), join_do_btn);
    gtk_box_append(GTK_BOX(join_outer), btns_box);
    gtk_revealer_set_child(GTK_REVEALER(join_rev), join_outer);
    gtk_box_append(GTK_BOX(popover_box), join_rev);

    /* ── Leave Session button ── */
    GtkWidget *leave_rev = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(leave_rev),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(leave_rev), FALSE);
    C.leave_revealer = GTK_REVEALER(leave_rev);

    GtkWidget *leave_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(leave_box, 12);
    gtk_widget_set_margin_end(leave_box, 12);
    gtk_widget_set_margin_top(leave_box, 4);
    gtk_widget_set_margin_bottom(leave_box, 12);

    GtkWidget *leave_btn = gtk_button_new_with_label(_("Leave Session"));
    gtk_widget_add_css_class(leave_btn, "destructive-action");
    gtk_widget_add_css_class(leave_btn, "pill");
    gtk_widget_set_hexpand(leave_btn, TRUE);
    g_signal_connect(leave_btn, "clicked", G_CALLBACK(on_primary_btn_clicked), NULL);
    C.primary_btn = GTK_BUTTON(leave_btn);

    gtk_box_append(GTK_BOX(leave_box), leave_btn);
    gtk_revealer_set_child(GTK_REVEALER(leave_rev), leave_box);
    gtk_box_append(GTK_BOX(popover_box), leave_rev);

    /* Attach popover */
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "collab-popover");
    gtk_popover_set_child(GTK_POPOVER(popover), clamp);
    gtk_menu_button_set_popover(self->btn_collab, popover);

    /* Stop the status spinner when the popover closes to avoid GTK
     * "Broken accounting of active state" warnings during dismissal. */
    g_signal_connect_swapped(popover, "closed",
                             G_CALLBACK(gtk_spinner_stop), C.status_spinner);
}

/* ------------------------------------------------------------------ */
/* Public: connect editor                                              */
/* ------------------------------------------------------------------ */

void silktex_collab_connect_editor(SilktexEditor *editor)
{
    /* Once a session is running, stay locked to the editor it was started
     * on — switching tabs or opening new ones must not move the session
     * to a different document. */
    if (C.in_session || C.session_pending) {
        collab_update_tab_indicator(TRUE);
        return;
    }

    collab_start_node(); /* no-op if already running */

    /* Disconnect signals from the old buffer. */
    if (C.buf) {
        if (C.insert_handler) g_signal_handler_disconnect(C.buf, C.insert_handler);
        if (C.delete_handler) g_signal_handler_disconnect(C.buf, C.delete_handler);
        if (C.cursor_handler) g_signal_handler_disconnect(C.buf, C.cursor_handler);
        g_object_remove_weak_pointer(G_OBJECT(C.buf), (gpointer *)&C.buf);
        C.buf            = NULL;
        C.insert_handler = 0;
        C.delete_handler = 0;
        C.cursor_handler = 0;
    }

    /* The tag pointers belong to the old buffer's tag table — invalidate them. */
    invalidate_peer_tags();

    if (C.editor)
        g_object_remove_weak_pointer(G_OBJECT(C.editor), (gpointer *)&C.editor);
    C.editor = editor;
    g_object_add_weak_pointer(G_OBJECT(editor), (gpointer *)&C.editor);

    GtkTextBuffer *buf = GTK_TEXT_BUFFER(silktex_editor_get_buffer(editor));
    C.buf = buf;
    g_object_add_weak_pointer(G_OBJECT(buf), (gpointer *)&C.buf);

    C.insert_handler = g_signal_connect(buf, "insert-text",
                                        G_CALLBACK(on_insert_text),              NULL);
    C.delete_handler = g_signal_connect(buf, "delete-range",
                                        G_CALLBACK(on_delete_range),             NULL);
    C.cursor_handler = g_signal_connect(buf, "notify::cursor-position",
                                        G_CALLBACK(on_cursor_position_changed),  NULL);
}

/* ------------------------------------------------------------------ */
/* Public: shutdown — called from window dispose so closing the window  */
/* leaves any active session and tears down the node subprocess.        */
/* ------------------------------------------------------------------ */

gboolean silktex_collab_is_bound_editor(SilktexEditor *editor)
{
    return editor != NULL && C.editor == editor && (C.in_session || C.session_pending);
}

void silktex_collab_leave_session(void)
{
    if (C.in_session || C.session_pending) do_leave_session();
}

void silktex_collab_shutdown(void)
{
    if (C.in_session || C.session_pending || C.proc) {
        do_leave_session();
    }
    if (C.window) {
        g_object_remove_weak_pointer(G_OBJECT(C.window), (gpointer *)&C.window);
        C.window = NULL;
    }
    /* The widgets below die with the window; drop them so a late callback
     * can't reach collab_update_ui with dangling pointers. */
    C.icon_stack       = NULL;
    C.btn_spinner      = NULL;
    C.peers_badge      = NULL;
    C.name_entry       = NULL;
    C.status_label     = NULL;
    C.status_spinner   = NULL;
    C.id_revealer      = NULL;
    C.session_id_entry = NULL;
    C.join_revealer    = NULL;
    C.join_entry       = NULL;
    C.leave_revealer   = NULL;
    C.primary_btn      = NULL;
}
