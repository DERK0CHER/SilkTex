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
    const char *shortcut; /* display string on right, NULL if none */
    const char *param;    /* if non-NULL, passed as "s" variant to action */
    const char *hint;     /* extra search terms, NULL if none */
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

/* LaTeX symbol entries — label is the LaTeX command, param is what gets inserted. */
static const CmdEntry SYMBOL_ENTRIES[] = {
    /* Greek lowercase */
    {"\\alpha",          "win.insert-latex", "α",  "\\alpha",          NULL},
    {"\\beta",           "win.insert-latex", "β",  "\\beta",           NULL},
    {"\\gamma",          "win.insert-latex", "γ",  "\\gamma",          NULL},
    {"\\delta",          "win.insert-latex", "δ",  "\\delta",          NULL},
    {"\\epsilon",        "win.insert-latex", "ε",  "\\epsilon",        NULL},
    {"\\varepsilon",     "win.insert-latex", "ε",  "\\varepsilon",     "epsilon"},
    {"\\zeta",           "win.insert-latex", "ζ",  "\\zeta",           NULL},
    {"\\eta",            "win.insert-latex", "η",  "\\eta",            NULL},
    {"\\theta",          "win.insert-latex", "θ",  "\\theta",          NULL},
    {"\\iota",           "win.insert-latex", "ι",  "\\iota",           NULL},
    {"\\kappa",          "win.insert-latex", "κ",  "\\kappa",          NULL},
    {"\\lambda",         "win.insert-latex", "λ",  "\\lambda",         NULL},
    {"\\mu",             "win.insert-latex", "μ",  "\\mu",             NULL},
    {"\\nu",             "win.insert-latex", "ν",  "\\nu",             NULL},
    {"\\xi",             "win.insert-latex", "ξ",  "\\xi",             NULL},
    {"\\pi",             "win.insert-latex", "π",  "\\pi",             NULL},
    {"\\rho",            "win.insert-latex", "ρ",  "\\rho",            NULL},
    {"\\sigma",          "win.insert-latex", "σ",  "\\sigma",          NULL},
    {"\\tau",            "win.insert-latex", "τ",  "\\tau",            NULL},
    {"\\phi",            "win.insert-latex", "φ",  "\\phi",            NULL},
    {"\\varphi",         "win.insert-latex", "φ",  "\\varphi",         "phi"},
    {"\\chi",            "win.insert-latex", "χ",  "\\chi",            NULL},
    {"\\psi",            "win.insert-latex", "ψ",  "\\psi",            NULL},
    {"\\omega",          "win.insert-latex", "ω",  "\\omega",          NULL},
    /* Greek uppercase */
    {"\\Gamma",          "win.insert-latex", "Γ",  "\\Gamma",          NULL},
    {"\\Delta",          "win.insert-latex", "Δ",  "\\Delta",          NULL},
    {"\\Theta",          "win.insert-latex", "Θ",  "\\Theta",          NULL},
    {"\\Lambda",         "win.insert-latex", "Λ",  "\\Lambda",         NULL},
    {"\\Xi",             "win.insert-latex", "Ξ",  "\\Xi",             NULL},
    {"\\Pi",             "win.insert-latex", "Π",  "\\Pi",             NULL},
    {"\\Sigma",          "win.insert-latex", "Σ",  "\\Sigma",          NULL},
    {"\\Phi",            "win.insert-latex", "Φ",  "\\Phi",            NULL},
    {"\\Psi",            "win.insert-latex", "Ψ",  "\\Psi",            NULL},
    {"\\Omega",          "win.insert-latex", "Ω",  "\\Omega",          NULL},
    /* Math structures */
    {"\\frac{}{}",       "win.insert-latex", NULL, "\\frac{}{}",       "fraction"},
    {"\\sqrt{}",         "win.insert-latex", "√",  "\\sqrt{}",         "square root"},
    {"\\sum",            "win.insert-latex", "∑",  "\\sum",            "summation"},
    {"\\int",            "win.insert-latex", "∫",  "\\int",            "integral"},
    {"\\prod",           "win.insert-latex", "∏",  "\\prod",           "product"},
    {"\\lim",            "win.insert-latex", NULL, "\\lim",            "limit"},
    {"\\infty",          "win.insert-latex", "∞",  "\\infty",          "infinity"},
    {"\\partial",        "win.insert-latex", "∂",  "\\partial",        "derivative"},
    {"\\nabla",          "win.insert-latex", "∇",  "\\nabla",          "gradient del"},
    /* Operators */
    {"\\pm",             "win.insert-latex", "±",  "\\pm",             "plus minus"},
    {"\\times",          "win.insert-latex", "×",  "\\times",          "multiply"},
    {"\\div",            "win.insert-latex", "÷",  "\\div",            "divide"},
    {"\\cdot",           "win.insert-latex", "·",  "\\cdot",           "dot"},
    /* Relations */
    {"\\leq",            "win.insert-latex", "≤",  "\\leq",            "less equal"},
    {"\\geq",            "win.insert-latex", "≥",  "\\geq",            "greater equal"},
    {"\\neq",            "win.insert-latex", "≠",  "\\neq",            "not equal"},
    {"\\approx",         "win.insert-latex", "≈",  "\\approx",         "approximately"},
    {"\\equiv",          "win.insert-latex", "≡",  "\\equiv",          "equivalent"},
    {"\\sim",            "win.insert-latex", "∼",  "\\sim",            "similar"},
    /* Sets */
    {"\\in",             "win.insert-latex", "∈",  "\\in",             "element set"},
    {"\\notin",          "win.insert-latex", "∉",  "\\notin",          "not element"},
    {"\\subset",         "win.insert-latex", "⊂",  "\\subset",         NULL},
    {"\\subseteq",       "win.insert-latex", "⊆",  "\\subseteq",       "subset equal"},
    {"\\cup",            "win.insert-latex", "∪",  "\\cup",            "union"},
    {"\\cap",            "win.insert-latex", "∩",  "\\cap",            "intersection"},
    {"\\emptyset",       "win.insert-latex", "∅",  "\\emptyset",       "empty set"},
    {"\\setminus",       "win.insert-latex", "∖",  "\\setminus",       "set difference"},
    /* Logic */
    {"\\forall",         "win.insert-latex", "∀",  "\\forall",         "for all"},
    {"\\exists",         "win.insert-latex", "∃",  "\\exists",         "there exists"},
    {"\\neg",            "win.insert-latex", "¬",  "\\neg",            "not negation"},
    {"\\wedge",          "win.insert-latex", "∧",  "\\wedge",          "and logical"},
    {"\\vee",            "win.insert-latex", "∨",  "\\vee",            "or logical"},
    /* Arrows */
    {"\\rightarrow",     "win.insert-latex", "→",  "\\rightarrow",     "arrow right to"},
    {"\\leftarrow",      "win.insert-latex", "←",  "\\leftarrow",      "arrow left from"},
    {"\\leftrightarrow", "win.insert-latex", "↔",  "\\leftrightarrow", "arrow both ways"},
    {"\\Rightarrow",     "win.insert-latex", "⇒",  "\\Rightarrow",     "implies double arrow"},
    {"\\Leftarrow",      "win.insert-latex", "⇐",  "\\Leftarrow",      "double arrow left"},
    {"\\Leftrightarrow", "win.insert-latex", "⇔",  "\\Leftrightarrow", "iff double arrow"},
    /* Accents and font commands */
    {"\\hat{}",          "win.insert-latex", NULL, "\\hat{}",          "hat circumflex"},
    {"\\vec{}",          "win.insert-latex", NULL, "\\vec{}",          "vector arrow"},
    {"\\bar{}",          "win.insert-latex", NULL, "\\bar{}",          "bar overline"},
    {"\\tilde{}",        "win.insert-latex", "~",  "\\tilde{}",        "tilde"},
    {"\\dot{}",          "win.insert-latex", NULL, "\\dot{}",          "dot derivative"},
    {"\\ddot{}",         "win.insert-latex", NULL, "\\ddot{}",         "double dot"},
    {"\\mathbf{}",       "win.insert-latex", NULL, "\\mathbf{}",       "bold"},
    {"\\mathbb{}",       "win.insert-latex", NULL, "\\mathbb{}",       "blackboard bold"},
    {"\\mathcal{}",      "win.insert-latex", NULL, "\\mathcal{}",      "calligraphic"},
    {"\\text{}",         "win.insert-latex", NULL, "\\text{}",         "text mode"},
};
/* clang-format on */

static const char *search_text = NULL;

static gboolean filter_row(GtkListBoxRow *row, gpointer ud)
{
    (void)ud;
    if (!search_text || !*search_text) return TRUE;

    g_autofree char *lower_query = g_utf8_casefold(search_text, -1);

    const char *label = g_object_get_data(G_OBJECT(row), "cmd-label");
    if (label) {
        g_autofree char *lower = g_utf8_casefold(label, -1);
        if (strstr(lower, lower_query)) return TRUE;
    }
    const char *shortcut = g_object_get_data(G_OBJECT(row), "cmd-shortcut");
    if (shortcut) {
        g_autofree char *lower = g_utf8_casefold(shortcut, -1);
        if (strstr(lower, lower_query)) return TRUE;
    }
    const char *hint = g_object_get_data(G_OBJECT(row), "cmd-hint");
    if (hint) {
        g_autofree char *lower = g_utf8_casefold(hint, -1);
        if (strstr(lower, lower_query)) return TRUE;
    }
    return FALSE;
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
    const char *param  = g_object_get_data(G_OBJECT(row), "cmd-param");
    if (!action) return;
    adw_dialog_close(dialog);
    if (param)
        gtk_widget_activate_action(window, action, "s", param);
    else
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

static GtkWidget *build_row(const CmdEntry *e)
{
    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(row_box, 4);
    gtk_widget_set_margin_bottom(row_box, 4);
    gtk_widget_set_margin_start(row_box, 8);
    gtk_widget_set_margin_end(row_box, 8);

    GtkWidget *lbl = gtk_label_new(e->label);
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
    g_object_set_data(G_OBJECT(row), "cmd-label",    (gpointer)e->label);
    g_object_set_data(G_OBJECT(row), "cmd-action",   (gpointer)e->action);
    g_object_set_data(G_OBJECT(row), "cmd-shortcut", (gpointer)e->shortcut);
    g_object_set_data(G_OBJECT(row), "cmd-param",    (gpointer)e->param);
    g_object_set_data(G_OBJECT(row), "cmd-hint",     (gpointer)e->hint);
    return row;
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
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search),
                                          _("Search commands and symbols…"));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
    gtk_widget_add_css_class(list, "navigation-sidebar");

    for (gsize i = 0; i < G_N_ELEMENTS(CMD_ENTRIES); i++)
        gtk_list_box_append(GTK_LIST_BOX(list), build_row(&CMD_ENTRIES[i]));

    for (gsize i = 0; i < G_N_ELEMENTS(SYMBOL_ENTRIES); i++)
        gtk_list_box_append(GTK_LIST_BOX(list), build_row(&SYMBOL_ENTRIES[i]));

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
