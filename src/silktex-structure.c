/*
 * SilkTex - Document outline sidebar
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "silktex-structure.h"
#include <glib/gi18n.h>
#include <string.h>

typedef struct {
    int         level;       /* 0=part 1=chapter 2=section … */
    int         line;        /* editor line (0-based) */
    char       *title;
} OutlineEntry;

struct _SilktexStructure {
    GtkWidget parent_instance;

    GtkWidget       *scrolled;
    GtkWidget       *listbox;
    GtkWidget       *empty_label;

    SilktexEditor   *editor;
    gulong           changed_handler;

    guint            refresh_idle;
};

G_DEFINE_FINAL_TYPE(SilktexStructure, silktex_structure, GTK_TYPE_WIDGET)

static void
outline_entry_free(gpointer p)
{
    OutlineEntry *e = p;
    if (!e) return;
    g_free(e->title);
    g_free(e);
}

/* Return structure level 0-5 for a LaTeX sectioning command, or -1. */
static int
match_level(const char *cmd)
{
    static const char * const names[] = {
        "part", "chapter", "section", "subsection",
        "subsubsection", "paragraph"
    };
    for (int i = 0; i < (int)G_N_ELEMENTS(names); i++) {
        if (g_strcmp0(cmd, names[i]) == 0) return i;
        /* Accept starred variants too e.g. \section* */
        g_autofree char *starred = g_strdup_printf("%s*", names[i]);
        if (g_strcmp0(cmd, starred) == 0) return i;
    }
    return -1;
}

/* Parse a single line and, if it is a structural command, append to list. */
static void
parse_line(const char *line, int lineno, GPtrArray *out)
{
    const char *p = line;
    while (*p && g_ascii_isspace(*p)) p++;
    if (*p != '\\') return;
    p++; /* skip backslash */

    /* read command name (letters + optional trailing *) */
    const char *start = p;
    while (*p && (g_ascii_isalpha(*p) || *p == '*')) p++;
    if (p == start) return;
    g_autofree char *cmd = g_strndup(start, p - start);
    int lvl = match_level(cmd);
    if (lvl < 0) return;

    /* Skip optional argument [..] */
    if (*p == '[') {
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            if (depth > 0) p++;
        }
        if (*p == ']') p++;
    }

    if (*p != '{') return;
    p++;

    /* read title until matching closing brace */
    int depth = 1;
    const char *t0 = p;
    while (*p && depth > 0) {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        if (depth > 0) p++;
    }
    if (p == t0) return;

    OutlineEntry *e = g_new0(OutlineEntry, 1);
    e->level = lvl;
    e->line  = lineno;
    e->title = g_strndup(t0, p - t0);
    g_ptr_array_add(out, e);
}

static void
on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer ud)
{
    SilktexStructure *self = SILKTEX_STRUCTURE(ud);
    OutlineEntry *e = g_object_get_data(G_OBJECT(row), "entry");
    if (!e || !self->editor) return;
    silktex_editor_goto_line(self->editor, e->line);
    gtk_widget_grab_focus(silktex_editor_get_view(self->editor));
}

static void
clear_listbox(SilktexStructure *self)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->listbox)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(self->listbox), child);
}

static void
populate(SilktexStructure *self, GPtrArray *entries)
{
    clear_listbox(self);

    if (entries->len == 0) {
        gtk_widget_set_visible(self->empty_label, TRUE);
        gtk_widget_set_visible(self->scrolled, FALSE);
        return;
    }
    gtk_widget_set_visible(self->empty_label, FALSE);
    gtk_widget_set_visible(self->scrolled, TRUE);

    for (guint i = 0; i < entries->len; i++) {
        OutlineEntry *e = g_ptr_array_index(entries, i);

        GtkWidget *row  = gtk_list_box_row_new();
        GtkWidget *lbl  = gtk_label_new(e->title);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_top(lbl, 4);
        gtk_widget_set_margin_bottom(lbl, 4);
        gtk_widget_set_margin_start(lbl, 8 + e->level * 14);
        gtk_widget_set_margin_end(lbl, 8);

        switch (e->level) {
            case 0: case 1:
                gtk_widget_add_css_class(lbl, "heading");
                break;
            case 2:
                /* plain label */
                break;
            default:
                gtk_widget_add_css_class(lbl, "dim-label");
                gtk_widget_add_css_class(lbl, "caption");
                break;
        }

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);

        OutlineEntry *copy = g_new0(OutlineEntry, 1);
        copy->level = e->level;
        copy->line  = e->line;
        copy->title = g_strdup(e->title);
        g_object_set_data_full(G_OBJECT(row), "entry", copy, outline_entry_free);

        gtk_list_box_append(GTK_LIST_BOX(self->listbox), row);
    }
}

static gboolean
refresh_cb(gpointer ud)
{
    SilktexStructure *self = SILKTEX_STRUCTURE(ud);
    self->refresh_idle = 0;

    if (!self->editor) {
        g_autoptr(GPtrArray) empty = g_ptr_array_new_with_free_func(outline_entry_free);
        populate(self, empty);
        return G_SOURCE_REMOVE;
    }

    g_autofree char *text = silktex_editor_get_text(self->editor);
    if (!text) text = g_strdup("");

    g_autoptr(GPtrArray) entries = g_ptr_array_new_with_free_func(outline_entry_free);
    int lineno = 0;
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        gsize len = nl ? (gsize)(nl - p) : strlen(p);
        g_autofree char *line = g_strndup(p, len);
        parse_line(line, lineno, entries);
        if (!nl) break;
        p = nl + 1;
        lineno++;
    }
    populate(self, entries);
    return G_SOURCE_REMOVE;
}

static void
schedule_refresh(SilktexStructure *self)
{
    if (self->refresh_idle > 0) return;
    self->refresh_idle = g_idle_add(refresh_cb, self);
}

static void
on_editor_changed(SilktexEditor *e, gpointer ud)
{
    schedule_refresh(SILKTEX_STRUCTURE(ud));
}

void
silktex_structure_set_editor(SilktexStructure *self, SilktexEditor *editor)
{
    g_return_if_fail(SILKTEX_IS_STRUCTURE(self));

    if (self->editor == editor) return;

    if (self->editor && self->changed_handler) {
        g_signal_handler_disconnect(self->editor, self->changed_handler);
        self->changed_handler = 0;
    }
    g_set_object(&self->editor, editor);
    if (editor) {
        self->changed_handler = g_signal_connect(editor, "changed",
                                                  G_CALLBACK(on_editor_changed),
                                                  self);
    }
    schedule_refresh(self);
}

void
silktex_structure_refresh(SilktexStructure *self)
{
    g_return_if_fail(SILKTEX_IS_STRUCTURE(self));
    schedule_refresh(self);
}

static void
silktex_structure_dispose(GObject *o)
{
    SilktexStructure *self = SILKTEX_STRUCTURE(o);
    if (self->refresh_idle) {
        g_source_remove(self->refresh_idle);
        self->refresh_idle = 0;
    }
    if (self->editor && self->changed_handler) {
        g_signal_handler_disconnect(self->editor, self->changed_handler);
        self->changed_handler = 0;
    }
    g_clear_object(&self->editor);

    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(o));
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_widget_unparent(child);
        child = next;
    }
    G_OBJECT_CLASS(silktex_structure_parent_class)->dispose(o);
}

static void
silktex_structure_class_init(SilktexStructureClass *klass)
{
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->dispose = silktex_structure_dispose;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass),
                                              GTK_TYPE_BOX_LAYOUT);
}

static void
silktex_structure_init(SilktexStructure *self)
{
    GtkBoxLayout *lm = GTK_BOX_LAYOUT(gtk_widget_get_layout_manager(GTK_WIDGET(self)));
    gtk_orientable_set_orientation(GTK_ORIENTABLE(lm), GTK_ORIENTATION_VERTICAL);

    self->scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(self->scrolled, TRUE);
    gtk_widget_set_parent(self->scrolled, GTK_WIDGET(self));

    self->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->listbox), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(self->listbox, "navigation-sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled), self->listbox);
    g_signal_connect(self->listbox, "row-activated",
                     G_CALLBACK(on_row_activated), self);

    self->empty_label = gtk_label_new(_("No sections yet"));
    gtk_widget_add_css_class(self->empty_label, "dim-label");
    gtk_widget_set_valign(self->empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(self->empty_label, TRUE);
    gtk_widget_set_visible(self->empty_label, TRUE);
    gtk_widget_set_parent(self->empty_label, GTK_WIDGET(self));
}

SilktexStructure *
silktex_structure_new(void)
{
    return g_object_new(SILKTEX_TYPE_STRUCTURE, NULL);
}
