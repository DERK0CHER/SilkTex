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
 *     {"cmd":"shutdown"}
 *
 *   node → C
 *     {"event":"session_ready","doc_id":"...","session_id":"<id>"}
 *     {"event":"remote_op","doc_id":"...","retain":<n>,"insert":"<s>"}
 *     {"event":"remote_op","doc_id":"...","retain":<n>,"delete":<n>}
 *     {"event":"snapshot","doc_id":"...","content":"<tex>"}
 *     {"event":"peer_count","doc_id":"...","count":<n>}
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
/* Global state (one node per process)                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    GSubprocess *proc;
    GDataInputStream *reader;
    GOutputStream *writer;
    GCancellable *cancel;

    SilktexEditor *editor; /* weak — cleared automatically on tab close */
    GtkTextBuffer *buf;    /* weak — buffer whose signals are connected  */
    gulong insert_handler;
    gulong delete_handler;
    char *doc_id;
    char *session_id;

    gboolean in_session;
    gboolean applying; /* TRUE while writing a remote op to the buffer */
    int peer_count;

    /* Header-bar button UI (owned here, attached to btn_collab) */
    GtkStack   *icon_stack;         /* "offline" / "online" pages           */
    GtkSpinner *btn_spinner;        /* spins in header button when searching */
    GtkLabel   *peers_badge;        /* peer count next to icon              */
    GtkLabel   *status_label;       /* inside popover — current state       */
    GtkSpinner *status_spinner;     /* spins in popover when searching      */
    GtkRevealer *id_revealer;       /* session-ID section (host only)       */
    AdwActionRow *session_id_row;   /* subtitle = the UUID, has copy suffix */
    GtkRevealer *join_revealer;     /* join + start/join buttons (no session) */
    GtkEditable *join_entry;        /* AdwEntryRow, implements GtkEditable  */
    GtkRevealer *leave_revealer;    /* leave button (in session)            */
    GtkButton   *primary_btn;       /* "Leave Session"                      */
} Collab;

static Collab C; /* zero-initialized */

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void schedule_read(void);
static void collab_update_ui(void);

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
    json_builder_set_member_name(b, "cmd");
    json_builder_add_string_value(b, "op");
    json_builder_set_member_name(b, "doc_id");
    json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "retain");
    json_builder_add_int_value(b, retain);
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

/* ------------------------------------------------------------------ */
/* Applying remote ops to the local buffer                             */
/* ------------------------------------------------------------------ */

static void apply_remote_op(int retain, const char *insert, int delete_count)
{
    if (!C.editor) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(C.editor);
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(sbuf);

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
    GError *err = NULL;
    gsize len = 0;
    char *line =
        g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(src), res, &len, &err);

    if (!line) {
        if (err && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("collab: node stream closed: %s", err->message);
        g_clear_error(&err);
        return;
    }

    JsonParser *p = json_parser_new();
    if (!json_parser_load_from_data(p, line, (gssize)len, NULL)) goto done;

    JsonNode *root = json_parser_get_root(p);
    if (!JSON_NODE_HOLDS_OBJECT(root)) goto done;

    JsonObject *obj = json_node_get_object(root);
    const char *event =
        json_object_has_member(obj, "event") ? json_object_get_string_member(obj, "event") : "";

    if (g_str_equal(event, "remote_op")) {
        int retain = json_object_has_member(obj, "retain")
                         ? (int)json_object_get_int_member(obj, "retain")
                         : 0;
        const char *ins = json_object_has_member(obj, "insert")
                              ? json_object_get_string_member(obj, "insert")
                              : NULL;
        int del = json_object_has_member(obj, "delete")
                      ? (int)json_object_get_int_member(obj, "delete")
                      : 0;
        apply_remote_op(retain, ins, del);
    } else if (g_str_equal(event, "snapshot")) {
        const char *content = json_object_has_member(obj, "content")
                                  ? json_object_get_string_member(obj, "content")
                                  : NULL;
        if (content && C.editor) {
            C.applying = TRUE;
            silktex_editor_set_text(C.editor, content, -1);
            C.applying = FALSE;
        }
    } else if (g_str_equal(event, "session_ready")) {
        const char *sid = json_object_has_member(obj, "session_id")
                              ? json_object_get_string_member(obj, "session_id")
                              : "?";
        C.in_session = TRUE;
        g_free(C.session_id);
        C.session_id = g_strdup(sid);
        collab_update_ui();
    } else if (g_str_equal(event, "peer_count")) {
        C.peer_count = json_object_has_member(obj, "count")
                           ? (int)json_object_get_int_member(obj, "count")
                           : 0;
        collab_update_ui();
    } else if (g_str_equal(event, "error")) {
        const char *msg =
            json_object_has_member(obj, "msg") ? json_object_get_string_member(obj, "msg") : "?";
        g_warning("silktex-node: %s", msg);
    }

done:
    g_object_unref(p);
    g_free(line);
    schedule_read();
}

static void schedule_read(void)
{
    if (!C.reader || !C.cancel) return;
    g_data_input_stream_read_line_async(C.reader, G_PRIORITY_DEFAULT, C.cancel, on_node_line, NULL);
}

/* ------------------------------------------------------------------ */
/* Buffer signal handlers                                              */
/* ------------------------------------------------------------------ */

static void on_insert_text(GtkTextBuffer *buf, GtkTextIter *loc, const char *text, int byte_len,
                           gpointer ud)
{
    (void)buf;
    (void)ud;
    if (C.applying || !C.in_session) return;

    int retain = gtk_text_iter_get_offset(loc);
    g_autofree char *safe = g_strndup(text, (gsize)byte_len);
    collab_send_op(retain, safe, 0);
}

static void on_delete_range(GtkTextBuffer *buf, GtkTextIter *start, GtkTextIter *end, gpointer ud)
{
    (void)buf;
    (void)ud;
    if (C.applying || !C.in_session) return;

    int s = gtk_text_iter_get_offset(start);
    int del = gtk_text_iter_get_offset(end) - s;
    if (del > 0) collab_send_op(s, NULL, del);
}

/* ------------------------------------------------------------------ */
/* Node lifecycle                                                      */
/* ------------------------------------------------------------------ */

static char *find_node_binary(void)
{
#ifdef __APPLE__
    char exe_buf[PATH_MAX];
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

    GError *err = NULL;
    const char *argv[] = {node, NULL};
    GSubprocess *proc =
        g_subprocess_newv((const char *const *)argv,
                          G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE, &err);

    if (!proc) {
        g_warning("collab: failed to start silktex-node: %s", err->message);
        g_clear_error(&err);
        return FALSE;
    }

    C.proc = proc;
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
    if (!C.editor || !collab_start_node()) return;

    g_free(C.doc_id);
    C.doc_id = g_uuid_string_random();

    g_autofree char *content = silktex_editor_get_text(C.editor);

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");
    json_builder_add_string_value(b, "create_session");
    json_builder_set_member_name(b, "doc_id");
    json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "content");
    json_builder_add_string_value(b, content ? content : "");
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
    if (!collab_start_node() || !session_id || !*session_id) return;

    g_free(C.doc_id);
    C.doc_id = g_uuid_string_random();

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "cmd");
    json_builder_add_string_value(b, "join_session");
    json_builder_set_member_name(b, "doc_id");
    json_builder_add_string_value(b, C.doc_id);
    json_builder_set_member_name(b, "session_id");
    json_builder_add_string_value(b, session_id);
    json_builder_end_object(b);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(b));
    g_autofree char *str = json_generator_to_data(gen, NULL);
    collab_send(str);
    g_object_unref(gen);
    g_object_unref(b);

    C.in_session = TRUE;
    g_free(C.session_id);
    C.session_id = g_strdup(session_id);
    collab_update_ui();
}

static void do_leave_session(void)
{
    /* Tell the node we're done, then reset state. */
    collab_send("{\"cmd\":\"shutdown\"}");
    g_cancellable_cancel(C.cancel);
    g_clear_object(&C.cancel);
    g_clear_object(&C.reader);
    g_clear_object(&C.proc);
    C.writer = NULL;
    C.in_session = FALSE;
    C.peer_count = 0;
    g_clear_pointer(&C.doc_id, g_free);
    g_clear_pointer(&C.session_id, g_free);
    collab_update_ui();
}

/* ------------------------------------------------------------------ */
/* Popover UI update                                                   */
/* ------------------------------------------------------------------ */

static void collab_update_ui(void)
{
    if (!C.icon_stack) return; /* UI not set up yet */

    if (C.in_session) {
        gtk_stack_set_visible_child_name(C.icon_stack, "online");

        gboolean waiting = (C.peer_count == 0);

        /* Spinner in header button */
        if (C.btn_spinner) {
            gtk_widget_set_visible(GTK_WIDGET(C.btn_spinner), waiting);
            if (waiting) gtk_spinner_start(C.btn_spinner);
            else         gtk_spinner_stop(C.btn_spinner);
        }

        /* Peer-count badge */
        if (C.peer_count > 0) {
            g_autofree char *badge = g_strdup_printf("%d", C.peer_count);
            gtk_label_set_text(C.peers_badge, badge);
            gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), TRUE);
        } else {
            gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), FALSE);
        }

        /* Status label + spinner in popover */
        g_autofree char *status = waiting
            ? g_strdup(_("Searching for peers on the network…"))
            : g_strdup_printf(C.peer_count == 1 ? _("%d peer connected")
                                                : _("%d peers connected"),
                              C.peer_count);
        gtk_label_set_text(C.status_label, status);
        if (C.status_spinner) {
            gtk_widget_set_visible(GTK_WIDGET(C.status_spinner), waiting);
            if (waiting) gtk_spinner_start(C.status_spinner);
            else         gtk_spinner_stop(C.status_spinner);
        }

        if (C.session_id_row)
            adw_action_row_set_subtitle(C.session_id_row, C.session_id ? C.session_id : "");

        gtk_revealer_set_reveal_child(C.id_revealer,   TRUE);
        gtk_revealer_set_reveal_child(C.join_revealer,  FALSE);
        gtk_revealer_set_reveal_child(C.leave_revealer, TRUE);
    } else {
        gtk_stack_set_visible_child_name(C.icon_stack, "offline");
        gtk_widget_set_visible(GTK_WIDGET(C.peers_badge), FALSE);

        if (C.btn_spinner) {
            gtk_spinner_stop(C.btn_spinner);
            gtk_widget_set_visible(GTK_WIDGET(C.btn_spinner), FALSE);
        }
        if (C.status_spinner) {
            gtk_spinner_stop(C.status_spinner);
            gtk_widget_set_visible(GTK_WIDGET(C.status_spinner), FALSE);
        }

        gtk_label_set_text(C.status_label, _("Not in a collaboration session"));
        gtk_revealer_set_reveal_child(C.id_revealer,   FALSE);
        gtk_revealer_set_reveal_child(C.join_revealer,  TRUE);
        gtk_revealer_set_reveal_child(C.leave_revealer, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* Popover button callbacks                                            */
/* ------------------------------------------------------------------ */

static void on_start_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn;
    (void)ud;
    do_create_session();
}

static void on_primary_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn;
    (void)ud;
    do_leave_session();
}

static void on_join_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn;
    (void)ud;
    if (!C.join_entry) return;
    const char *sid = gtk_editable_get_text(C.join_entry);
    if (sid && *sid) {
        do_join_session(sid);
        gtk_editable_set_text(C.join_entry, "");
    }
}

/* AdwEntryRow fires "apply" when the user presses Enter. */
static void on_join_apply(AdwEntryRow *row, gpointer ud)
{
    (void)row;
    (void)ud;
    on_join_btn_clicked(NULL, NULL);
}

static void on_copy_btn_clicked(GtkButton *btn, gpointer ud)
{
    (void)btn;
    (void)ud;
    if (!C.session_id) return;
    GdkClipboard *cb = gdk_display_get_clipboard(gdk_display_get_default());
    gdk_clipboard_set_text(cb, C.session_id);
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

    /* Spinner shown while searching for peers */
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

    /* Status row: icon · label · spinner */
    GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(status_row, 12);
    gtk_widget_set_margin_end(status_row, 12);
    gtk_widget_set_margin_top(status_row, 12);
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

    GtkWidget *id_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(id_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(id_list, "boxed-list");
    gtk_widget_set_margin_start(id_list, 12);
    gtk_widget_set_margin_end(id_list, 12);
    gtk_widget_set_margin_top(id_list, 8);
    gtk_widget_set_margin_bottom(id_list, 4);

    GtkWidget *id_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(id_row), _("Session ID"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(id_row), "");
    adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(id_row), TRUE);
    C.session_id_row = ADW_ACTION_ROW(id_row);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_add_css_class(copy_btn, "flat");
    gtk_widget_add_css_class(copy_btn, "circular");
    gtk_widget_set_tooltip_text(copy_btn, _("Copy session ID"));
    gtk_widget_set_valign(copy_btn, GTK_ALIGN_CENTER);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_btn_clicked), NULL);
    adw_action_row_add_suffix(ADW_ACTION_ROW(id_row), copy_btn);

    gtk_list_box_append(GTK_LIST_BOX(id_list), id_row);
    gtk_revealer_set_child(GTK_REVEALER(id_rev), id_list);
    gtk_box_append(GTK_BOX(popover_box), id_rev);

    /* ── Not-in-session section: entry row + Start/Join buttons ── */
    GtkWidget *join_rev = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(join_rev),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(join_rev), TRUE);
    C.join_revealer = GTK_REVEALER(join_rev);

    GtkWidget *join_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Entry row for session ID */
    GtkWidget *join_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(join_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(join_list, "boxed-list");
    gtk_widget_set_margin_start(join_list, 12);
    gtk_widget_set_margin_end(join_list, 12);
    gtk_widget_set_margin_top(join_list, 8);
    gtk_widget_set_margin_bottom(join_list, 0);

    GtkWidget *join_entry_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(join_entry_row), _("Paste Session ID to join"));
    C.join_entry = GTK_EDITABLE(join_entry_row);
    g_signal_connect(join_entry_row, "apply", G_CALLBACK(on_join_apply), NULL);
    gtk_list_box_append(GTK_LIST_BOX(join_list), join_entry_row);
    gtk_box_append(GTK_BOX(join_outer), join_list);

    /* Two action buttons: Start Session | Join */
    GtkWidget *btns_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(btns_box, 12);
    gtk_widget_set_margin_end(btns_box, 12);
    gtk_widget_set_margin_top(btns_box, 8);
    gtk_widget_set_margin_bottom(btns_box, 12);

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

    /* ── Leave Session button (shown when in session) ── */
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

    if (C.buf) {
        g_signal_handler_disconnect(C.buf, C.insert_handler);
        g_signal_handler_disconnect(C.buf, C.delete_handler);
        g_object_remove_weak_pointer(G_OBJECT(C.buf), (gpointer *)&C.buf);
        C.buf = NULL;
        C.insert_handler = 0;
        C.delete_handler = 0;
    }

    if (C.editor) g_object_remove_weak_pointer(G_OBJECT(C.editor), (gpointer *)&C.editor);
    C.editor = editor;
    g_object_add_weak_pointer(G_OBJECT(editor), (gpointer *)&C.editor);

    GtkTextBuffer *buf = GTK_TEXT_BUFFER(silktex_editor_get_buffer(editor));
    C.buf = buf;
    g_object_add_weak_pointer(G_OBJECT(buf), (gpointer *)&C.buf);
    C.insert_handler = g_signal_connect(buf, "insert-text", G_CALLBACK(on_insert_text), NULL);
    C.delete_handler = g_signal_connect(buf, "delete-range", G_CALLBACK(on_delete_range), NULL);
}
