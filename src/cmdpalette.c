/*
 * SilkTex - Command palette
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "cmdpalette.h"
#include "i18n.h"

typedef struct {
    const char *label;
    const char *action;
    const char *shortcut; /* display string, NULL if none */
} CmdEntry;

/* clang-format off */
static const CmdEntry CMD_ENTRIES[] = {
    {N_("New Document"),           "win.new",              "Ctrl+N"},
    {N_("Open File…"),             "win.open",             "Ctrl+O"},
    {N_("Save"),                   "win.save",             "Ctrl+S"},
    {N_("Save As…"),               "win.save-as",          "Ctrl+Shift+S"},
    {N_("Compile"),                "win.compile",          "Ctrl+Enter"},
    {N_("Undo"),                   "win.undo",             "Ctrl+Z"},
    {N_("Redo"),                   "win.redo",             "Ctrl+Shift+Z"},
    {N_("Bold"),                   "win.bold",             "Ctrl+B"},
    {N_("Italic"),                 "win.italic",           "Ctrl+I"},
    {N_("Underline"),              "win.underline",        "Ctrl+U"},
    {N_("Find"),                   "win.find",             "Ctrl+Shift+F"},
    {N_("Find and Replace"),       "win.find-replace",     "Ctrl+F"},
    {N_("Forward Sync"),           "win.forward-sync",     "Ctrl+Alt+F"},
    {N_("Editor Zoom In"),         "win.editor-zoom-in",   "Ctrl++"},
    {N_("Editor Zoom Out"),        "win.editor-zoom-out",  "Ctrl+-"},
    {N_("Editor Zoom Reset"),      "win.editor-zoom-reset","Ctrl+Shift+0"},
    {N_("Preview Zoom In"),        "win.zoom-in",          NULL},
    {N_("Preview Zoom Out"),       "win.zoom-out",         NULL},
    {N_("Preview Zoom Fit Width"), "win.zoom-fit",         "Ctrl+0"},
    {N_("Preview Zoom Fit Page"),  "win.zoom-fit-page",    NULL},
    {N_("Preview Zoom Reset"),     "win.zoom-reset",       NULL},
    {N_("Previous Page"),          "win.prev-page",        "PgUp"},
    {N_("Next Page"),              "win.next-page",        "PgDn"},
    {N_("Toggle Preview"),         "win.toggle-preview",   "F9"},
    {N_("Toggle Sidebar"),         "win.toggle-sidebar",   "F8"},
    {N_("Toggle Compile Log"),     "win.toggle-log",       NULL},
    {N_("Fullscreen"),             "win.fullscreen",       "F11"},
    {N_("Preferences"),            "win.preferences",      "Ctrl+,"},
    {N_("Keyboard Shortcuts"),     "win.shortcuts",        "Ctrl+?"},
    {N_("Run BibTeX"),             "win.run-bibtex",       NULL},
    {N_("Run MakeIndex"),          "win.run-makeindex",    NULL},
    {N_("Cleanup Auxiliary Files"),"win.cleanup",          NULL},
    {N_("Document Statistics"),    "win.stats",            NULL},
    {N_("Open PDF Externally"),    "win.open-pdf-external",NULL},
    {N_("Insert Image…"),          "win.insert-image",     NULL},
    {N_("Insert Table…"),          "win.insert-table",     NULL},
    {N_("Insert Matrix…"),         "win.insert-matrix",    NULL},
    {N_("Insert Bibliography…"),   "win.insert-biblio",    NULL},
    {N_("Insert itemize"),         "win.insert-itemize",   NULL},
    {N_("Insert enumerate"),       "win.insert-enumerate", NULL},
    {N_("Insert description"),     "win.insert-description",NULL},
    {N_("Insert equation"),        "win.insert-equation",  NULL},
    {N_("Insert quote"),           "win.insert-quote",     NULL},
};
/* clang-format on */

static const char *search_text = NULL;

static gboolean filter_row(GtkListBoxRow *row, gpointer ud)
{
    (void)ud;
    if (!search_text || !*search_text) return TRUE;
    const char *label = g_object_get_data(G_OBJECT(row), "cmd-label");
    if (!label) return FALSE;
    g_autofree char *lower_label = g_utf8_casefold(label, -1);
    g_autofree char *lower_query = g_utf8_casefold(search_text, -1);
    return strstr(lower_label, lower_query) != NULL;
}

static void on_search_changed(GtkEditable *e, gpointer ud)
{
    GtkListBox *list = GTK_LIST_BOX(ud);
    search_text = gtk_editable_get_text(e);
    gtk_list_box_invalidate_filter(list);

    /* Auto-select first visible row. */
    for (int i = 0; ; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(list, i);
        if (!row) break;
        if (gtk_widget_get_visible(GTK_WIDGET(row))) {
            gtk_list_box_select_row(list, row);
            break;
        }
    }
}

static void activate_selected(GtkListBox *list, GtkWidget *window, AdwDialog *dialog)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(list);
    if (!row) return;
    const char *action = g_object_get_data(G_OBJECT(row), "cmd-action");
    if (!action) return;
    adw_dialog_close(dialog);
    gtk_widget_activate_action(window, action, NULL);
}

static void on_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer ud)
{
    (void)row;
    AdwDialog **ctx = ud;
    AdwDialog *dialog = ctx[0];
    GtkWidget *window = GTK_WIDGET(ctx[1]);
    activate_selected(list, window, dialog);
}

static gboolean on_search_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                      GdkModifierType state, gpointer ud)
{
    (void)ctrl; (void)keycode; (void)state;
    GtkListBox *list = GTK_LIST_BOX(((gpointer *)ud)[2]);
    AdwDialog *dialog = ((gpointer *)ud)[0];
    GtkWidget *window = GTK_WIDGET(((gpointer *)ud)[1]);

    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        activate_selected(list, window, dialog);
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Down) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(list);
        int idx = sel ? gtk_list_box_row_get_index(sel) + 1 : 0;
        for (;;idx++) {
            GtkListBoxRow *next = gtk_list_box_get_row_at_index(list, idx);
            if (!next) break;
            if (gtk_widget_get_visible(GTK_WIDGET(next))) {
                gtk_list_box_select_row(list, next);
                break;
            }
        }
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Up) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(list);
        int idx = sel ? gtk_list_box_row_get_index(sel) - 1 : -1;
        for (;idx >= 0; idx--) {
            GtkListBoxRow *prev = gtk_list_box_get_row_at_index(list, idx);
            if (!prev) break;
            if (gtk_widget_get_visible(GTK_WIDGET(prev))) {
                gtk_list_box_select_row(list, prev);
                break;
            }
        }
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Escape) {
        adw_dialog_close(dialog);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void free_ctx_notify(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

void silktex_cmd_palette_show(GtkWidget *window)
{
    search_text = NULL;

    AdwDialog *dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, _("Command Palette"));
    adw_dialog_set_content_width(dialog, 480);
    adw_dialog_set_content_height(dialog, 420);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_set_margin_top(search, 8);
    gtk_widget_set_margin_bottom(search, 8);
    gtk_widget_set_margin_start(search, 8);
    gtk_widget_set_margin_end(search, 8);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), _("Search commands…"));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
    gtk_widget_add_css_class(list, "navigation-sidebar");

    for (gsize i = 0; i < G_N_ELEMENTS(CMD_ENTRIES); i++) {
        const CmdEntry *e = &CMD_ENTRIES[i];

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_margin_top(row_box, 4);
        gtk_widget_set_margin_bottom(row_box, 4);
        gtk_widget_set_margin_start(row_box, 8);
        gtk_widget_set_margin_end(row_box, 8);

        GtkWidget *lbl = gtk_label_new(_(e->label));
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_box_append(GTK_BOX(row_box), lbl);

        if (e->shortcut) {
            GtkWidget *sc_lbl = gtk_label_new(e->shortcut);
            gtk_widget_add_css_class(sc_lbl, "dim-label");
            gtk_widget_add_css_class(sc_lbl, "caption");
            gtk_box_append(GTK_BOX(row_box), sc_lbl);
        }

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        g_object_set_data(G_OBJECT(row), "cmd-label", (gpointer)_(e->label));
        g_object_set_data(G_OBJECT(row), "cmd-action", (gpointer)e->action);
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }

    /* Select first row by default. */
    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
    if (first) gtk_list_box_select_row(GTK_LIST_BOX(list), first);

    gtk_list_box_set_filter_func(GTK_LIST_BOX(list), filter_row, NULL, NULL);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(vbox), search);
    gtk_box_append(GTK_BOX(vbox), scroll);
    adw_dialog_set_child(dialog, vbox);

    /* Context array: [dialog, window, list] — owned by the search key controller closure. */
    gpointer *ctx = g_new(gpointer, 3);
    ctx[0] = dialog;
    ctx[1] = window;
    ctx[2] = list;

    g_signal_connect(search, "search-changed", G_CALLBACK(on_search_changed), list);

    gpointer *row_ctx = g_new0(gpointer, 2);
    row_ctx[0] = dialog;
    row_ctx[1] = window;
    g_signal_connect_data(list, "row-activated", G_CALLBACK(on_row_activated), row_ctx,
                          free_ctx_notify, 0);

    GtkEventControllerKey *key_ctrl =
        GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key_ctrl),
                                               GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(search, GTK_EVENT_CONTROLLER(key_ctrl));
    g_signal_connect_data(key_ctrl, "key-pressed", G_CALLBACK(on_search_key_pressed), ctx,
                          free_ctx_notify, 0);

    adw_dialog_present(dialog, window);
    gtk_widget_grab_focus(search);
}
