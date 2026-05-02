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
 *     {"event":"remote_op","doc_id":"...","retain":<n>,"insert":"<s>"}
 *     {"event":"remote_op","doc_id":"...","retain":<n>,"delete":<n>}
 *     {"event":"snapshot","doc_id":"...","content":"<tex>"}
 *     {"event":"peer_count","doc_id":"...","count":<n>}
 *     {"event":"remote_cursor","doc_id":"...","peer_id":"...","offset":<n>}
 *     {"event":"peer_name","peer_id":"...","name":"<display name>"}
 *     {"event":"error","msg":"<text>"}
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
    gboolean          applying;        /* TRUE while writing a remote op    */
    int               peer_count;

    GHashTable       *peers;           /* peer_id → PeerState*              */
    int               next_color_idx;  /* round-robin color assignment      */
    guint             cursor_debounce; /* GSource ID for cursor broadcast   */

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
    if (!C.buf) return;

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

static void collab_send_op(int retain, const char *insert, int delete_count)
{
    if (!C.in_session || !C.doc_id) return;

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
    json_generator_set_root(gen, json_builder_get_root(b));
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);
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
    json_generator_set_root(gen, json_builder_get_root(b));
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
    json_generator_set_root(gen, json_builder_get_root(b));
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
    if (!C.editor) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(C.editor);
    GtkTextBuffer   *buf  = GTK_TEXT_BUFFER(sbuf);

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
        if (err && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("collab: node stream closed: %s", err->message);
        C.session_pending = FALSE;
        C.in_session      = FALSE;
        C.peer_count      = 0;
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

    if (g_str_equal(event, "remote_op")) {
        int         retain = json_object_has_member(obj, "retain")
                                 ? (int)json_object_get_int_member(obj, "retain") : 0;
        const char *ins    = json_object_has_member(obj, "insert")
                                 ? json_object_get_string_member(obj, "insert") : NULL;
        int         del    = json_object_has_member(obj, "delete")
                                 ? (int)json_object_get_int_member(obj, "delete") : 0;
        apply_remote_op(retain, ins, del);

    } else if (g_str_equal(event, "snapshot")) {
        const char *content = json_object_has_member(obj, "content")
                                  ? json_object_get_string_member(obj, "content") : NULL;
        if (content && C.editor) {
            C.applying = TRUE;
            silktex_editor_set_text(C.editor, content, -1);
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
        C.peer_count = json_object_has_member(obj, "count")
                           ? (int)json_object_get_int_member(obj, "count") : 0;
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
    if (C.applying || !C.in_session) return;
    int retain = gtk_text_iter_get_offset(loc);
    g_autofree char *safe = g_strndup(text, (gsize)byte_len);
    collab_send_op(retain, safe, 0);
}

static void on_delete_range(GtkTextBuffer *buf, GtkTextIter *start, GtkTextIter *end,
                            gpointer ud)
{
    (void)buf; (void)ud;
    if (C.applying || !C.in_session) return;
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
    C.peer_count      = 0;
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
    json_generator_set_root(gen, json_builder_get_root(b));
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
    C.peer_count      = 0;
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
    json_generator_set_root(gen, json_builder_get_root(b));
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
    const char *sid = gtk_editable_get_text(C.join_entry);
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
