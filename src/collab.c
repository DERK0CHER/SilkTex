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
    GSubprocess      *proc;
    GDataInputStream *reader;
    GOutputStream    *writer;
    GCancellable     *cancel;

    SilktexEditor    *editor;   /* weak — cleared automatically on tab close */
    char             *doc_id;
    char             *session_id;

    gboolean          in_session;
    gboolean          applying;   /* TRUE while writing a remote op to the buffer */
    int               peer_count;

    /* Header-bar button UI (owned here, attached to btn_collab) */
    GtkStack    *icon_stack;     /* "offline" / "online" pages            */
    GtkLabel    *peers_badge;    /* peer count next to icon               */
    GtkLabel    *status_label;   /* inside popover — "Not in session" etc */
    GtkRevealer *id_revealer;    /* session-ID row (host only)            */
    GtkLabel    *session_id_lbl; /* the actual UUID label                 */
    GtkRevealer *join_revealer;  /* join-entry row (before session)       */
    GtkEntry    *join_entry;     /* session-ID text entry                 */
    GtkButton   *primary_btn;    /* "Start Session" or "Leave Session"    */
    GtkListBox  *peer_list;      /* connected peers                       */
} Collab;

static Collab C;   /* zero-initialized */

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void schedule_read (void);
static void collab_update_ui (void);

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

static void
collab_send (const char *json)
{
    if (!C.writer) return;
    GError *err = NULL;
    g_autofree char *line = g_strconcat (json, "\n", NULL);
    g_output_stream_write_all (C.writer, line, strlen (line), NULL, NULL, &err);
    if (err) {
        g_warning ("collab: send error: %s", err->message);
        g_clear_error (&err);
    }
}

static void
collab_send_op (int retain, const char *insert, int delete_count)
{
    if (!C.in_session || !C.doc_id) return;

    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    json_builder_set_member_name (b, "cmd");    json_builder_add_string_value (b, "op");
    json_builder_set_member_name (b, "doc_id"); json_builder_add_string_value (b, C.doc_id);
    json_builder_set_member_name (b, "retain"); json_builder_add_int_value (b, retain);
    if (insert)
    {
        json_builder_set_member_name (b, "insert");
        json_builder_add_string_value (b, insert);
    }
    if (delete_count > 0)
    {
        json_builder_set_member_name (b, "delete");
        json_builder_add_int_value (b, delete_count);
    }
    json_builder_end_object (b);

    JsonGenerator *gen = json_generator_new ();
    json_generator_set_root (gen, json_builder_get_root (b));
    g_autofree char *str = json_generator_to_data (gen, NULL);
    collab_send (str);

    g_object_unref (gen);
    g_object_unref (b);
}

/* ------------------------------------------------------------------ */
/* Applying remote ops to the local buffer                             */
/* ------------------------------------------------------------------ */

static void
apply_remote_op (int retain, const char *insert, int delete_count)
{
    if (!C.editor) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer (C.editor);
    GtkTextBuffer   *buf  = GTK_TEXT_BUFFER (sbuf);

    C.applying = TRUE;
    gtk_text_buffer_begin_irreversible_action (buf);

    GtkTextIter it;
    gtk_text_buffer_get_iter_at_offset (buf, &it, retain);

    if (delete_count > 0)
    {
        GtkTextIter end = it;
        gtk_text_iter_forward_chars (&end, delete_count);
        gtk_text_buffer_delete (buf, &it, &end);
        gtk_text_buffer_get_iter_at_offset (buf, &it, retain);
    }
    if (insert && *insert)
        gtk_text_buffer_insert (buf, &it, insert, -1);

    gtk_text_buffer_end_irreversible_action (buf);
    C.applying = FALSE;
}

/* ------------------------------------------------------------------ */
/* Reading events from the node                                        */
/* ------------------------------------------------------------------ */

static void
on_node_line (GObject *src, GAsyncResult *res, gpointer ud)
{
    GError *err  = NULL;
    gsize   len  = 0;
    char   *line = g_data_input_stream_read_line_finish_utf8 (
                        G_DATA_INPUT_STREAM (src), res, &len, &err);

    if (!line)
    {
        if (err && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning ("collab: node stream closed: %s", err->message);
        g_clear_error (&err);
        return;
    }

    JsonParser *p = json_parser_new ();
    if (!json_parser_load_from_data (p, line, (gssize) len, NULL))
        goto done;

    JsonNode *root = json_parser_get_root (p);
    if (!JSON_NODE_HOLDS_OBJECT (root))
        goto done;

    JsonObject *obj   = json_node_get_object (root);
    const char *event = json_object_has_member (obj, "event")
                        ? json_object_get_string_member (obj, "event") : "";

    if (g_str_equal (event, "remote_op"))
    {
        int         retain = json_object_has_member (obj, "retain")
                             ? (int) json_object_get_int_member (obj, "retain") : 0;
        const char *ins    = json_object_has_member (obj, "insert")
                             ? json_object_get_string_member (obj, "insert") : NULL;
        int         del    = json_object_has_member (obj, "delete")
                             ? (int) json_object_get_int_member (obj, "delete") : 0;
        apply_remote_op (retain, ins, del);
    }
    else if (g_str_equal (event, "snapshot"))
    {
        const char *content = json_object_has_member (obj, "content")
                              ? json_object_get_string_member (obj, "content") : NULL;
        if (content && C.editor)
        {
            C.applying = TRUE;
            silktex_editor_set_text (C.editor, content, -1);
            C.applying = FALSE;
        }
    }
    else if (g_str_equal (event, "session_ready"))
    {
        const char *sid = json_object_has_member (obj, "session_id")
                          ? json_object_get_string_member (obj, "session_id") : "?";
        C.in_session = TRUE;
        g_free (C.session_id);
        C.session_id = g_strdup (sid);
        collab_update_ui ();
    }
    else if (g_str_equal (event, "peer_count"))
    {
        C.peer_count = json_object_has_member (obj, "count")
                       ? (int) json_object_get_int_member (obj, "count") : 0;
        collab_update_ui ();
    }
    else if (g_str_equal (event, "error"))
    {
        const char *msg = json_object_has_member (obj, "msg")
                          ? json_object_get_string_member (obj, "msg") : "?";
        g_warning ("silktex-node: %s", msg);
    }

done:
    g_object_unref (p);
    g_free (line);
    schedule_read ();
}

static void
schedule_read (void)
{
    if (!C.reader || !C.cancel) return;
    g_data_input_stream_read_line_async (C.reader, G_PRIORITY_DEFAULT,
                                         C.cancel, on_node_line, NULL);
}

/* ------------------------------------------------------------------ */
/* Buffer signal handlers                                              */
/* ------------------------------------------------------------------ */

static void
on_insert_text (GtkTextBuffer *buf, GtkTextIter *loc,
                const char *text, int byte_len, gpointer ud)
{
    (void) buf; (void) ud;
    if (C.applying || !C.in_session) return;

    int retain = gtk_text_iter_get_offset (loc);
    g_autofree char *safe = g_strndup (text, (gsize) byte_len);
    collab_send_op (retain, safe, 0);
}

static void
on_delete_range (GtkTextBuffer *buf, GtkTextIter *start, GtkTextIter *end, gpointer ud)
{
    (void) buf; (void) ud;
    if (C.applying || !C.in_session) return;

    int s   = gtk_text_iter_get_offset (start);
    int del = gtk_text_iter_get_offset (end) - s;
    if (del > 0)
        collab_send_op (s, NULL, del);
}

/* ------------------------------------------------------------------ */
/* Node lifecycle                                                      */
/* ------------------------------------------------------------------ */

static char *
find_node_binary (void)
{
#ifdef __APPLE__
    char    exe_buf[PATH_MAX];
    uint32_t size = PATH_MAX;
    if (_NSGetExecutablePath (exe_buf, &size) == 0)
    {
        g_autofree char *dir = g_path_get_dirname (exe_buf);
        g_autofree char *adj = g_build_filename (dir, "silktex-node", NULL);
        if (g_file_test (adj, G_FILE_TEST_IS_EXECUTABLE))
            return g_strdup (adj);
    }
#else
    g_autofree char *self = g_file_read_link ("/proc/self/exe", NULL);
    if (self)
    {
        g_autofree char *dir = g_path_get_dirname (self);
        g_autofree char *adj = g_build_filename (dir, "silktex-node", NULL);
        if (g_file_test (adj, G_FILE_TEST_IS_EXECUTABLE))
            return g_strdup (adj);
    }
#endif
    return g_find_program_in_path ("silktex-node");
}

static gboolean
collab_start_node (void)
{
    if (C.proc) return TRUE;

    g_autofree char *node = find_node_binary ();
    if (!node)
    {
        g_message ("collab: silktex-node not found; collaboration disabled");
        return FALSE;
    }

    GError      *err  = NULL;
    const char  *argv[] = { node, NULL };
    GSubprocess *proc   = g_subprocess_newv (
        (const char * const *) argv,
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
        &err);

    if (!proc)
    {
        g_warning ("collab: failed to start silktex-node: %s", err->message);
        g_clear_error (&err);
        return FALSE;
    }

    C.proc   = proc;
    C.cancel = g_cancellable_new ();
    C.writer = g_subprocess_get_stdin_pipe (proc);
    C.reader = g_data_input_stream_new (g_subprocess_get_stdout_pipe (proc));
    schedule_read ();
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Session commands                                                    */
/* ------------------------------------------------------------------ */

static void
do_create_session (void)
{
    if (!C.editor || !collab_start_node ()) return;

    g_free (C.doc_id);
    C.doc_id = g_uuid_string_random ();

    g_autofree char *content = silktex_editor_get_text (C.editor);

    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    json_builder_set_member_name (b, "cmd");     json_builder_add_string_value (b, "create_session");
    json_builder_set_member_name (b, "doc_id");  json_builder_add_string_value (b, C.doc_id);
    json_builder_set_member_name (b, "content"); json_builder_add_string_value (b, content ? content : "");
    json_builder_end_object (b);

    JsonGenerator *gen = json_generator_new ();
    json_generator_set_root (gen, json_builder_get_root (b));
    g_autofree char *str = json_generator_to_data (gen, NULL);
    collab_send (str);
    g_object_unref (gen);
    g_object_unref (b);
}

static void
do_join_session (const char *session_id)
{
    if (!collab_start_node () || !session_id || !*session_id) return;

    g_free (C.doc_id);
    C.doc_id = g_uuid_string_random ();

    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);
    json_builder_set_member_name (b, "cmd");        json_builder_add_string_value (b, "join_session");
    json_builder_set_member_name (b, "doc_id");     json_builder_add_string_value (b, C.doc_id);
    json_builder_set_member_name (b, "session_id"); json_builder_add_string_value (b, session_id);
    json_builder_end_object (b);

    JsonGenerator *gen = json_generator_new ();
    json_generator_set_root (gen, json_builder_get_root (b));
    g_autofree char *str = json_generator_to_data (gen, NULL);
    collab_send (str);
    g_object_unref (gen);
    g_object_unref (b);

    C.in_session = TRUE;
    g_free (C.session_id);
    C.session_id = g_strdup (session_id);
    collab_update_ui ();
}

static void
do_leave_session (void)
{
    /* Tell the node we're done, then reset state. */
    collab_send ("{\"cmd\":\"shutdown\"}");
    g_cancellable_cancel (C.cancel);
    g_clear_object (&C.cancel);
    g_clear_object (&C.reader);
    g_clear_object (&C.proc);
    C.writer     = NULL;
    C.in_session = FALSE;
    C.peer_count = 0;
    g_clear_pointer (&C.doc_id,     g_free);
    g_clear_pointer (&C.session_id, g_free);
    collab_update_ui ();
}

/* ------------------------------------------------------------------ */
/* Popover UI update                                                   */
/* ------------------------------------------------------------------ */

static void
collab_update_ui (void)
{
    if (!C.icon_stack) return;   /* UI not set up yet */

    if (C.in_session)
    {
        gtk_stack_set_visible_child_name (C.icon_stack, "online");

        if (C.peer_count > 0)
        {
            g_autofree char *badge = g_strdup_printf ("%d", C.peer_count);
            gtk_label_set_text (C.peers_badge, badge);
            gtk_widget_set_visible (GTK_WIDGET (C.peers_badge), TRUE);
        }
        else
        {
            gtk_widget_set_visible (GTK_WIDGET (C.peers_badge), FALSE);
        }

        g_autofree char *status =
            C.peer_count == 0
            ? g_strdup ("Waiting for peers…")
            : g_strdup_printf ("%d peer%s connected",
                               C.peer_count, C.peer_count == 1 ? "" : "s");
        gtk_label_set_text (C.status_label, status);

        if (C.session_id_lbl)
            gtk_label_set_text (C.session_id_lbl, C.session_id ? C.session_id : "");
        gtk_revealer_set_reveal_child (C.id_revealer,   TRUE);
        gtk_revealer_set_reveal_child (C.join_revealer, FALSE);
        gtk_button_set_label (C.primary_btn, "Leave Session");
        gtk_widget_add_css_class (GTK_WIDGET (C.primary_btn), "destructive-action");
        gtk_widget_remove_css_class (GTK_WIDGET (C.primary_btn), "suggested-action");
    }
    else
    {
        gtk_stack_set_visible_child_name (C.icon_stack, "offline");
        gtk_widget_set_visible (GTK_WIDGET (C.peers_badge), FALSE);
        gtk_label_set_text (C.status_label, "Not in session");
        gtk_revealer_set_reveal_child (C.id_revealer,   FALSE);
        gtk_revealer_set_reveal_child (C.join_revealer, TRUE);
        gtk_button_set_label (C.primary_btn, "Start Session");
        gtk_widget_add_css_class (GTK_WIDGET (C.primary_btn), "suggested-action");
        gtk_widget_remove_css_class (GTK_WIDGET (C.primary_btn), "destructive-action");
    }
}

/* ------------------------------------------------------------------ */
/* Popover button callbacks                                            */
/* ------------------------------------------------------------------ */

static void
on_primary_btn_clicked (GtkButton *btn, gpointer ud)
{
    (void) btn; (void) ud;
    if (C.in_session)
        do_leave_session ();
    else
        do_create_session ();
}

static void
on_join_btn_clicked (GtkButton *btn, gpointer ud)
{
    (void) btn; (void) ud;
    if (!C.join_entry) return;
    const char *sid = gtk_editable_get_text (GTK_EDITABLE (C.join_entry));
    if (sid && *sid)
    {
        do_join_session (sid);
        gtk_editable_set_text (GTK_EDITABLE (C.join_entry), "");
    }
}

static void
on_copy_btn_clicked (GtkButton *btn, gpointer ud)
{
    (void) btn; (void) ud;
    if (!C.session_id) return;
    GdkClipboard *cb = gdk_display_get_clipboard (gdk_display_get_default ());
    gdk_clipboard_set_text (cb, C.session_id);
}

/* ------------------------------------------------------------------ */
/* Public: silktex_collab_setup_window                                 */
/* ------------------------------------------------------------------ */

void
silktex_collab_setup_window (SilktexWindow *self)
{
    if (!self->btn_collab) return;

    /* ── Button child: icon stack + peer badge ── */
    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);

    GtkWidget *stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    C.icon_stack = GTK_STACK (stack);

    /* offline page */
    GtkWidget *offline_img = gtk_image_new_from_icon_name ("system-users-symbolic");
    gtk_widget_add_css_class (offline_img, "dim-label");
    gtk_stack_add_named (GTK_STACK (stack), offline_img, "offline");

    /* online page */
    GtkWidget *online_img = gtk_image_new_from_icon_name ("system-users-symbolic");
    gtk_stack_add_named (GTK_STACK (stack), online_img, "online");

    gtk_stack_set_visible_child_name (GTK_STACK (stack), "offline");

    GtkWidget *badge = gtk_label_new ("");
    gtk_widget_add_css_class (badge, "caption");
    gtk_widget_add_css_class (badge, "numeric");
    gtk_widget_set_visible (badge, FALSE);
    C.peers_badge = GTK_LABEL (badge);

    gtk_box_append (GTK_BOX (btn_box), stack);
    gtk_box_append (GTK_BOX (btn_box), badge);
    gtk_menu_button_set_child (self->btn_collab, btn_box);

    /* ── Popover ── */
    GtkWidget *popover_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    /* Clamp to keep it narrow */
    GtkWidget *clamp = adw_clamp_new ();
    adw_clamp_set_maximum_size (ADW_CLAMP (clamp), 280);
    adw_clamp_set_child (ADW_CLAMP (clamp), popover_box);

    /* Status row */
    GtkWidget *status_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start  (status_row, 12);
    gtk_widget_set_margin_end    (status_row, 12);
    gtk_widget_set_margin_top    (status_row, 12);
    gtk_widget_set_margin_bottom (status_row, 8);

    GtkWidget *status_icon = gtk_image_new_from_icon_name ("system-users-symbolic");
    GtkWidget *status_lbl  = gtk_label_new ("Not in session");
    gtk_label_set_xalign (GTK_LABEL (status_lbl), 0.0f);
    gtk_widget_set_hexpand (status_lbl, TRUE);
    C.status_label = GTK_LABEL (status_lbl);

    gtk_box_append (GTK_BOX (status_row), status_icon);
    gtk_box_append (GTK_BOX (status_row), status_lbl);
    gtk_box_append (GTK_BOX (popover_box), status_row);

    /* Separator */
    gtk_box_append (GTK_BOX (popover_box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

    /* Session ID revealer (host only) */
    GtkWidget *id_rev = gtk_revealer_new ();
    gtk_revealer_set_transition_type (GTK_REVEALER (id_rev), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child (GTK_REVEALER (id_rev), FALSE);
    C.id_revealer = GTK_REVEALER (id_rev);

    GtkWidget *id_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start  (id_row, 12);
    gtk_widget_set_margin_end    (id_row, 12);
    gtk_widget_set_margin_top    (id_row, 10);
    gtk_widget_set_margin_bottom (id_row, 4);

    GtkWidget *id_lbl = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (id_lbl), 0.0f);
    gtk_widget_set_hexpand (id_lbl, TRUE);
    gtk_label_set_ellipsize (GTK_LABEL (id_lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable (GTK_LABEL (id_lbl), TRUE);
    gtk_widget_add_css_class (id_lbl, "monospace");
    gtk_widget_add_css_class (id_lbl, "caption");
    C.session_id_lbl = GTK_LABEL (id_lbl);

    GtkWidget *copy_btn = gtk_button_new_from_icon_name ("edit-copy-symbolic");
    gtk_widget_add_css_class (copy_btn, "flat");
    gtk_widget_add_css_class (copy_btn, "circular");
    gtk_widget_set_tooltip_text (copy_btn, "Copy session ID");
    g_signal_connect (copy_btn, "clicked", G_CALLBACK (on_copy_btn_clicked), NULL);

    gtk_box_append (GTK_BOX (id_row), id_lbl);
    gtk_box_append (GTK_BOX (id_row), copy_btn);
    gtk_revealer_set_child (GTK_REVEALER (id_rev), id_row);
    gtk_box_append (GTK_BOX (popover_box), id_rev);

    /* Join revealer (shown when not in session) */
    GtkWidget *join_rev = gtk_revealer_new ();
    gtk_revealer_set_transition_type (GTK_REVEALER (join_rev), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child (GTK_REVEALER (join_rev), TRUE);
    C.join_revealer = GTK_REVEALER (join_rev);

    GtkWidget *join_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start  (join_row, 12);
    gtk_widget_set_margin_end    (join_row, 12);
    gtk_widget_set_margin_top    (join_row, 10);
    gtk_widget_set_margin_bottom (join_row, 4);

    GtkWidget *join_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (join_entry), "Session ID to join…");
    gtk_widget_set_hexpand (join_entry, TRUE);
    C.join_entry = GTK_ENTRY (join_entry);

    GtkWidget *join_btn = gtk_button_new_with_label ("Join");
    gtk_widget_add_css_class (join_btn, "suggested-action");
    g_signal_connect (join_btn, "clicked", G_CALLBACK (on_join_btn_clicked), NULL);

    gtk_box_append (GTK_BOX (join_row), join_entry);
    gtk_box_append (GTK_BOX (join_row), join_btn);
    gtk_revealer_set_child (GTK_REVEALER (join_rev), join_row);
    gtk_box_append (GTK_BOX (popover_box), join_rev);

    /* Primary action button (Start / Leave) */
    GtkWidget *action_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start  (action_row, 12);
    gtk_widget_set_margin_end    (action_row, 12);
    gtk_widget_set_margin_top    (action_row, 10);
    gtk_widget_set_margin_bottom (action_row, 12);

    GtkWidget *primary_btn = gtk_button_new_with_label ("Start Session");
    gtk_widget_add_css_class (primary_btn, "suggested-action");
    gtk_widget_set_hexpand (primary_btn, TRUE);
    g_signal_connect (primary_btn, "clicked", G_CALLBACK (on_primary_btn_clicked), NULL);
    C.primary_btn = GTK_BUTTON (primary_btn);

    gtk_box_append (GTK_BOX (action_row), primary_btn);
    gtk_box_append (GTK_BOX (popover_box), action_row);

    /* Peer list (scrollable, hidden until in session) */
    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scroll), 180);
    gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scroll), TRUE);
    gtk_widget_set_margin_bottom (scroll, 8);
    gtk_widget_set_visible (scroll, FALSE);   /* shown when peers arrive */

    GtkWidget *peer_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (peer_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class (peer_list, "boxed-list");
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), peer_list);
    C.peer_list = GTK_LIST_BOX (peer_list);
    gtk_box_append (GTK_BOX (popover_box), scroll);

    /* Attach popover to the button */
    GtkWidget *popover = gtk_popover_new ();
    gtk_widget_add_css_class (popover, "collab-popover");
    gtk_popover_set_child (GTK_POPOVER (popover), clamp);
    gtk_menu_button_set_popover (self->btn_collab, popover);
}

/* ------------------------------------------------------------------ */
/* Public: connect editor                                              */
/* ------------------------------------------------------------------ */

void
silktex_collab_connect_editor (SilktexEditor *editor)
{
    collab_start_node ();   /* no-op if already running */

    if (C.editor)
        g_object_remove_weak_pointer (G_OBJECT (C.editor), (gpointer *) &C.editor);
    C.editor = editor;
    g_object_add_weak_pointer (G_OBJECT (editor), (gpointer *) &C.editor);

    GtkTextBuffer *buf = GTK_TEXT_BUFFER (silktex_editor_get_buffer (editor));
    g_signal_connect (buf, "insert-text",  G_CALLBACK (on_insert_text),  NULL);
    g_signal_connect (buf, "delete-range", G_CALLBACK (on_delete_range), NULL);
}
