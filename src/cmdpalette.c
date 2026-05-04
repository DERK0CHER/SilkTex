/*
 * SilkTex - Command palette
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "cmdpalette.h"
#include "i18n.h"

#define CAT_COMMAND 0
#define CAT_LATEX   1

typedef struct {
    const char *label;
    const char *action;
    const char *shortcut;    /* display on right of row; glyph for symbols, kbd for commands */
    const char *param;       /* if non-NULL, passed as "s" variant to action */
    const char *hint;        /* extra search terms */
    const char *description; /* shown in preview pane */
} CmdEntry;

typedef struct {
    AdwDialog  *dialog;
    GtkWidget  *window;
    GtkListBox *list;
    GtkLabel   *preview_glyph;
    GtkWidget  *preview_glyph_box;
    GtkLabel   *preview_title;
    GtkLabel   *preview_desc;
    GtkWidget  *preview_content;
    GtkWidget  *preview_empty;
    GtkWidget  *preview_code_scroll;
    GtkLabel   *preview_code;
} PaletteCtx;

/* clang-format off */
static const CmdEntry CMD_ENTRIES[] = {
    {N_("New Document"),           "win.new",              "Ctrl+N",       NULL, NULL, NULL},
    {N_("Open File…"),             "win.open",             "Ctrl+O",       NULL, NULL, NULL},
    {N_("Save"),                   "win.save",             "Ctrl+S",       NULL, NULL, NULL},
    {N_("Save As…"),               "win.save-as",          "Ctrl+Shift+S", NULL, NULL, NULL},
    {N_("Compile"),                "win.compile",          "Ctrl+Enter",   NULL, NULL,
        N_("Run pdflatex on the current document")},
    {N_("Undo"),                   "win.undo",             "Ctrl+Z",       NULL, NULL, NULL},
    {N_("Redo"),                   "win.redo",             "Ctrl+Shift+Z", NULL, NULL, NULL},
    {N_("Bold"),                   "win.bold",             "Ctrl+B",       NULL, NULL, NULL},
    {N_("Italic"),                 "win.italic",           "Ctrl+I",       NULL, NULL, NULL},
    {N_("Underline"),              "win.underline",        "Ctrl+U",       NULL, NULL, NULL},
    {N_("Find"),                   "win.find",             "Ctrl+Shift+F", NULL, NULL, NULL},
    {N_("Find and Replace"),       "win.find-replace",     "Ctrl+F",       NULL, NULL, NULL},
    {N_("Forward Sync"),           "win.forward-sync",     "Ctrl+Alt+F",   NULL, NULL,
        N_("Jump to cursor position in the PDF preview")},
    {N_("Editor Zoom In"),         "win.editor-zoom-in",   "Ctrl++",       NULL, NULL, NULL},
    {N_("Editor Zoom Out"),        "win.editor-zoom-out",  "Ctrl+-",       NULL, NULL, NULL},
    {N_("Editor Zoom Reset"),      "win.editor-zoom-reset","Ctrl+Shift+0", NULL, NULL, NULL},
    {N_("Preview Zoom In"),        "win.zoom-in",          NULL,           NULL, NULL, NULL},
    {N_("Preview Zoom Out"),       "win.zoom-out",         NULL,           NULL, NULL, NULL},
    {N_("Preview Zoom Fit Width"), "win.zoom-fit",         "Ctrl+0",       NULL, NULL, NULL},
    {N_("Preview Zoom Fit Page"),  "win.zoom-fit-page",    NULL,           NULL, NULL, NULL},
    {N_("Preview Zoom Reset"),     "win.zoom-reset",       NULL,           NULL, NULL, NULL},
    {N_("Previous Page"),          "win.prev-page",        "PgUp",         NULL, NULL, NULL},
    {N_("Next Page"),              "win.next-page",        "PgDn",         NULL, NULL, NULL},
    {N_("Toggle Preview"),         "win.toggle-preview",   "F9",           NULL, NULL, NULL},
    {N_("Toggle Sidebar"),         "win.toggle-sidebar",   "F8",           NULL, NULL, NULL},
    {N_("Toggle Compile Log"),     "win.toggle-log",       NULL,           NULL, NULL, NULL},
    {N_("Fullscreen"),             "win.fullscreen",       "F11",          NULL, NULL, NULL},
    {N_("Preferences"),            "win.preferences",      "Ctrl+,",       NULL, NULL, NULL},
    {N_("Keyboard Shortcuts"),     "win.shortcuts",        "Ctrl+?",       NULL, NULL,
        N_("Open this palette to search commands and symbols")},
    {N_("Run BibTeX"),             "win.run-bibtex",       NULL,           NULL, NULL, NULL},
    {N_("Run MakeIndex"),          "win.run-makeindex",    NULL,           NULL, NULL, NULL},
    {N_("Cleanup Auxiliary Files"),"win.cleanup",          NULL,           NULL, NULL, NULL},
    {N_("Document Statistics"),    "win.stats",            NULL,           NULL, NULL, NULL},
    {N_("Open PDF Externally"),    "win.open-pdf-external",NULL,           NULL, NULL, NULL},
    {N_("Insert Image…"),          "win.insert-image",     NULL,           NULL, NULL, NULL},
    {N_("Insert Table…"),          "win.insert-table",     NULL,           NULL, NULL, NULL},
    {N_("Insert Matrix…"),         "win.insert-matrix",    NULL,           NULL, NULL, NULL},
    {N_("Insert Bibliography…"),   "win.insert-biblio",    NULL,           NULL, NULL, NULL},
    {N_("Insert itemize"),         "win.insert-itemize",   NULL,           NULL, NULL, NULL},
    {N_("Insert enumerate"),       "win.insert-enumerate", NULL,           NULL, NULL, NULL},
    {N_("Insert description"),     "win.insert-description",NULL,          NULL, NULL, NULL},
    {N_("Insert equation"),        "win.insert-equation",  NULL,           NULL, NULL, NULL},
    {N_("Insert quote"),           "win.insert-quote",     NULL,           NULL, NULL, NULL},
};

static const CmdEntry SYMBOL_ENTRIES[] = {
    /* ── Greek lowercase ──────────────────────────────────────────────────── */
    {"\\alpha",          "win.insert-latex", "α", "\\alpha",          NULL, N_("Greek lowercase alpha")},
    {"\\beta",           "win.insert-latex", "β", "\\beta",           NULL, N_("Greek lowercase beta")},
    {"\\gamma",          "win.insert-latex", "γ", "\\gamma",          NULL, N_("Greek lowercase gamma")},
    {"\\delta",          "win.insert-latex", "δ", "\\delta",          NULL, N_("Greek lowercase delta")},
    {"\\epsilon",        "win.insert-latex", "ε", "\\epsilon",        NULL, N_("Greek lowercase epsilon")},
    {"\\varepsilon",     "win.insert-latex", "ε", "\\varepsilon",     "epsilon", N_("Greek lowercase epsilon (variant)")},
    {"\\zeta",           "win.insert-latex", "ζ", "\\zeta",           NULL, N_("Greek lowercase zeta")},
    {"\\eta",            "win.insert-latex", "η", "\\eta",            NULL, N_("Greek lowercase eta")},
    {"\\theta",          "win.insert-latex", "θ", "\\theta",          NULL, N_("Greek lowercase theta")},
    {"\\iota",           "win.insert-latex", "ι", "\\iota",           NULL, N_("Greek lowercase iota")},
    {"\\kappa",          "win.insert-latex", "κ", "\\kappa",          NULL, N_("Greek lowercase kappa")},
    {"\\lambda",         "win.insert-latex", "λ", "\\lambda",         NULL, N_("Greek lowercase lambda")},
    {"\\mu",             "win.insert-latex", "μ", "\\mu",             NULL, N_("Greek lowercase mu")},
    {"\\nu",             "win.insert-latex", "ν", "\\nu",             NULL, N_("Greek lowercase nu")},
    {"\\xi",             "win.insert-latex", "ξ", "\\xi",             NULL, N_("Greek lowercase xi")},
    {"\\pi",             "win.insert-latex", "π", "\\pi",             NULL, N_("Greek lowercase pi")},
    {"\\rho",            "win.insert-latex", "ρ", "\\rho",            NULL, N_("Greek lowercase rho")},
    {"\\sigma",          "win.insert-latex", "σ", "\\sigma",          NULL, N_("Greek lowercase sigma")},
    {"\\tau",            "win.insert-latex", "τ", "\\tau",            NULL, N_("Greek lowercase tau")},
    {"\\phi",            "win.insert-latex", "φ", "\\phi",            NULL, N_("Greek lowercase phi")},
    {"\\varphi",         "win.insert-latex", "φ", "\\varphi",         "phi", N_("Greek lowercase phi (variant)")},
    {"\\chi",            "win.insert-latex", "χ", "\\chi",            NULL, N_("Greek lowercase chi")},
    {"\\psi",            "win.insert-latex", "ψ", "\\psi",            NULL, N_("Greek lowercase psi")},
    {"\\omega",          "win.insert-latex", "ω", "\\omega",          NULL, N_("Greek lowercase omega")},
    /* ── Greek uppercase ──────────────────────────────────────────────────── */
    {"\\Gamma",          "win.insert-latex", "Γ", "\\Gamma",          NULL, N_("Greek uppercase Gamma")},
    {"\\Delta",          "win.insert-latex", "Δ", "\\Delta",          NULL, N_("Greek uppercase Delta")},
    {"\\Theta",          "win.insert-latex", "Θ", "\\Theta",          NULL, N_("Greek uppercase Theta")},
    {"\\Lambda",         "win.insert-latex", "Λ", "\\Lambda",         NULL, N_("Greek uppercase Lambda")},
    {"\\Xi",             "win.insert-latex", "Ξ", "\\Xi",             NULL, N_("Greek uppercase Xi")},
    {"\\Pi",             "win.insert-latex", "Π", "\\Pi",             NULL, N_("Greek uppercase Pi")},
    {"\\Sigma",          "win.insert-latex", "Σ", "\\Sigma",          NULL, N_("Greek uppercase Sigma")},
    {"\\Phi",            "win.insert-latex", "Φ", "\\Phi",            NULL, N_("Greek uppercase Phi")},
    {"\\Psi",            "win.insert-latex", "Ψ", "\\Psi",            NULL, N_("Greek uppercase Psi")},
    {"\\Omega",          "win.insert-latex", "Ω", "\\Omega",          NULL, N_("Greek uppercase Omega")},
    /* ── Math structures ──────────────────────────────────────────────────── */
    {"\\frac{}{}",       "win.insert-latex", NULL, "\\frac{}{}",      "fraction", N_("Fraction — numerator over denominator")},
    {"\\sqrt{}",         "win.insert-latex", "√",  "\\sqrt{}",        "square root", N_("Square root")},
    {"\\sum",            "win.insert-latex", "∑",  "\\sum",           "summation", N_("Summation")},
    {"\\int",            "win.insert-latex", "∫",  "\\int",           "integral", N_("Integral")},
    {"\\prod",           "win.insert-latex", "∏",  "\\prod",          "product", N_("Product")},
    {"\\lim",            "win.insert-latex", NULL, "\\lim",           "limit", N_("Limit")},
    {"\\infty",          "win.insert-latex", "∞",  "\\infty",         "infinity", N_("Infinity")},
    {"\\partial",        "win.insert-latex", "∂",  "\\partial",       "derivative", N_("Partial derivative")},
    {"\\nabla",          "win.insert-latex", "∇",  "\\nabla",         "gradient del", N_("Nabla / gradient operator")},
    /* ── Operators ────────────────────────────────────────────────────────── */
    {"\\pm",             "win.insert-latex", "±",  "\\pm",            "plus minus", N_("Plus-minus")},
    {"\\times",          "win.insert-latex", "×",  "\\times",         "multiply", N_("Multiplication sign")},
    {"\\div",            "win.insert-latex", "÷",  "\\div",           "divide", N_("Division sign")},
    {"\\cdot",           "win.insert-latex", "·",  "\\cdot",          "dot multiply", N_("Centre dot (multiplication)")},
    /* ── Relations ────────────────────────────────────────────────────────── */
    {"\\leq",            "win.insert-latex", "≤",  "\\leq",           "less equal", N_("Less than or equal to")},
    {"\\geq",            "win.insert-latex", "≥",  "\\geq",           "greater equal", N_("Greater than or equal to")},
    {"\\neq",            "win.insert-latex", "≠",  "\\neq",           "not equal", N_("Not equal to")},
    {"\\approx",         "win.insert-latex", "≈",  "\\approx",        "approximately", N_("Approximately equal to")},
    {"\\equiv",          "win.insert-latex", "≡",  "\\equiv",         "equivalent", N_("Equivalent to / identically equal")},
    {"\\sim",            "win.insert-latex", "∼",  "\\sim",           "similar", N_("Similar to / tilde relation")},
    /* ── Sets ─────────────────────────────────────────────────────────────── */
    {"\\in",             "win.insert-latex", "∈",  "\\in",            "element set", N_("Element of a set")},
    {"\\notin",          "win.insert-latex", "∉",  "\\notin",         "not element", N_("Not an element of")},
    {"\\subset",         "win.insert-latex", "⊂",  "\\subset",        NULL, N_("Proper subset of")},
    {"\\subseteq",       "win.insert-latex", "⊆",  "\\subseteq",      "subset equal", N_("Subset of or equal to")},
    {"\\cup",            "win.insert-latex", "∪",  "\\cup",           "union", N_("Set union")},
    {"\\cap",            "win.insert-latex", "∩",  "\\cap",           "intersection", N_("Set intersection")},
    {"\\emptyset",       "win.insert-latex", "∅",  "\\emptyset",      "empty set", N_("Empty set")},
    {"\\setminus",       "win.insert-latex", "∖",  "\\setminus",      "set difference", N_("Set difference")},
    /* ── Logic ────────────────────────────────────────────────────────────── */
    {"\\forall",         "win.insert-latex", "∀",  "\\forall",        "for all", N_("For all (universal quantifier)")},
    {"\\exists",         "win.insert-latex", "∃",  "\\exists",        "there exists", N_("There exists (existential quantifier)")},
    {"\\neg",            "win.insert-latex", "¬",  "\\neg",           "not negation", N_("Logical negation")},
    {"\\wedge",          "win.insert-latex", "∧",  "\\wedge",         "and logical", N_("Logical and (conjunction)")},
    {"\\vee",            "win.insert-latex", "∨",  "\\vee",           "or logical", N_("Logical or (disjunction)")},
    /* ── Arrows ───────────────────────────────────────────────────────────── */
    {"\\rightarrow",     "win.insert-latex", "→",  "\\rightarrow",    "arrow right to", N_("Right arrow (implies / maps to)")},
    {"\\leftarrow",      "win.insert-latex", "←",  "\\leftarrow",     "arrow left from", N_("Left arrow")},
    {"\\leftrightarrow", "win.insert-latex", "↔",  "\\leftrightarrow","arrow both ways", N_("Left-right arrow")},
    {"\\Rightarrow",     "win.insert-latex", "⇒",  "\\Rightarrow",    "implies double arrow", N_("Double right arrow (implies)")},
    {"\\Leftarrow",      "win.insert-latex", "⇐",  "\\Leftarrow",     "double arrow left", N_("Double left arrow")},
    {"\\Leftrightarrow", "win.insert-latex", "⇔",  "\\Leftrightarrow","iff double arrow", N_("Double left-right arrow (if and only if)")},
    /* ── Accents & fonts ──────────────────────────────────────────────────── */
    {"\\hat{}",          "win.insert-latex", NULL, "\\hat{}",         "hat circumflex", N_("Hat accent (circumflex)")},
    {"\\vec{}",          "win.insert-latex", NULL, "\\vec{}",         "vector arrow over", N_("Vector arrow accent")},
    {"\\bar{}",          "win.insert-latex", NULL, "\\bar{}",         "bar overline", N_("Bar / overline accent")},
    {"\\tilde{}",        "win.insert-latex", "~",  "\\tilde{}",       "tilde", N_("Tilde accent")},
    {"\\dot{}",          "win.insert-latex", NULL, "\\dot{}",         "dot time derivative", N_("Dot accent (first derivative)")},
    {"\\ddot{}",         "win.insert-latex", NULL, "\\ddot{}",        "double dot", N_("Double dot accent (second derivative)")},
    {"\\mathbf{}",       "win.insert-latex", NULL, "\\mathbf{}",      "bold", N_("Bold math font")},
    {"\\mathbb{}",       "win.insert-latex", NULL, "\\mathbb{}",      "blackboard bold double struck", N_("Blackboard bold font (amsfonts)")},
    {"\\mathcal{}",      "win.insert-latex", NULL, "\\mathcal{}",     "calligraphic script", N_("Calligraphic font")},
    {"\\text{}",         "win.insert-latex", NULL, "\\text{}",        "text mode roman", N_("Text mode inside math (amsmath)")},
    /* ── Environments (multi-line templates) ─────────────────────────────── */
    {"\\begin{equation}", "win.insert-latex", NULL,
        "\\begin{equation}\n  \n\\end{equation}",
        "equation environment", N_("Numbered equation environment")},
    {"\\begin{align}",   "win.insert-latex", NULL,
        "\\begin{align}\n  f(x) &= \\\\\n        &= \n\\end{align}",
        "align aligned equations", N_("Multi-line aligned equations (amsmath)")},
    {"\\begin{cases}",   "win.insert-latex", NULL,
        "\\begin{cases}\n  f(x) & x > 0 \\\\\n  0    & x \\leq 0\n\\end{cases}",
        "cases piecewise function", N_("Piecewise / cases environment (amsmath)")},
    {"\\begin{pmatrix}", "win.insert-latex", "( )",
        "\\begin{pmatrix}\n  a & b \\\\\n  c & d\n\\end{pmatrix}",
        "matrix parentheses pmatrix", N_("Matrix with parentheses (amsmath)")},
    {"\\begin{bmatrix}", "win.insert-latex", "[ ]",
        "\\begin{bmatrix}\n  a & b \\\\\n  c & d\n\\end{bmatrix}",
        "matrix brackets bmatrix", N_("Matrix with square brackets (amsmath)")},
    {"\\begin{vmatrix}", "win.insert-latex", "| |",
        "\\begin{vmatrix}\n  a & b \\\\\n  c & d\n\\end{vmatrix}",
        "determinant vmatrix", N_("Determinant / vertical bar matrix (amsmath)")},
    {"\\begin{Bmatrix}", "win.insert-latex", "{ }",
        "\\begin{Bmatrix}\n  a & b \\\\\n  c & d\n\\end{Bmatrix}",
        "matrix curly braces", N_("Matrix with curly braces (amsmath)")},
};
/* clang-format on */

static const char *search_text = NULL;

/* ── Filter ──────────────────────────────────────────────────────────────── */

static gboolean filter_row(GtkListBoxRow *row, gpointer ud)
{
    (void)ud;
    if (!search_text || !*search_text) return TRUE;

    g_autofree char *qfold = g_utf8_casefold(search_text, -1);

    const char *fields[] = {
        g_object_get_data(G_OBJECT(row), "cmd-label"),
        g_object_get_data(G_OBJECT(row), "cmd-shortcut"),
        g_object_get_data(G_OBJECT(row), "cmd-hint"),
        g_object_get_data(G_OBJECT(row), "cmd-description"),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(fields); i++) {
        if (!fields[i]) continue;
        g_autofree char *fold = g_utf8_casefold(fields[i], -1);
        if (strstr(fold, qfold)) return TRUE;
    }
    return FALSE;
}

/* ── Section headers ─────────────────────────────────────────────────────── */

static void update_header_func(GtkListBoxRow *row, GtkListBoxRow *before, gpointer data)
{
    (void)data;
    int cat  = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),    "cmd-category"));
    int prev = before ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(before), "cmd-category")) : -1;

    if (cat == prev) {
        gtk_list_box_row_set_header(row, NULL);
        return;
    }

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (before) {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(sep, 6);
        gtk_box_append(GTK_BOX(box), sep);
    }
    const char *title = (cat == CAT_COMMAND) ? _("Commands") : _("LaTeX Symbols");
    GtkWidget *lbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_start(lbl, 8);
    gtk_widget_set_margin_top(lbl, 6);
    gtk_widget_set_margin_bottom(lbl, 2);
    gtk_widget_add_css_class(lbl, "heading");
    gtk_widget_add_css_class(lbl, "dim-label");
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_list_box_row_set_header(row, box);
}

/* ── Preview ─────────────────────────────────────────────────────────────── */

static void update_preview(GtkListBoxRow *row, PaletteCtx *ctx)
{
    if (!row) {
        gtk_widget_set_visible(ctx->preview_empty,   TRUE);
        gtk_widget_set_visible(ctx->preview_content, FALSE);
        return;
    }

    const char *label    = g_object_get_data(G_OBJECT(row), "cmd-label");
    const char *shortcut = g_object_get_data(G_OBJECT(row), "cmd-shortcut");
    const char *param    = g_object_get_data(G_OBJECT(row), "cmd-param");
    const char *desc     = g_object_get_data(G_OBJECT(row), "cmd-description");
    int         cat      = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "cmd-category"));

    gtk_widget_set_visible(ctx->preview_empty,   FALSE);
    gtk_widget_set_visible(ctx->preview_content, TRUE);

    /* Glyph / shortcut box */
    gboolean show_glyph = (shortcut != NULL);
    gtk_widget_set_visible(ctx->preview_glyph_box, show_glyph);
    if (show_glyph) {
        gtk_label_set_text(ctx->preview_glyph, shortcut);
        /* Large style for Unicode glyphs, smaller for keyboard shortcuts */
        if (cat == CAT_LATEX)
            gtk_widget_add_css_class(GTK_WIDGET(ctx->preview_glyph), "title-2");
        else
            gtk_widget_remove_css_class(GTK_WIDGET(ctx->preview_glyph), "title-2");
    }

    /* Title */
    gtk_label_set_text(ctx->preview_title, label ? label : "");

    /* Description */
    gboolean show_desc = (desc != NULL && *desc);
    gtk_widget_set_visible(GTK_WIDGET(ctx->preview_desc), show_desc);
    if (show_desc)
        gtk_label_set_text(ctx->preview_desc, _(desc));

    /* Code preview: show for all LaTeX entries that have a param */
    gboolean show_code = (cat == CAT_LATEX && param != NULL);
    gtk_widget_set_visible(ctx->preview_code_scroll, show_code);
    if (show_code)
        gtk_label_set_text(ctx->preview_code, param);
}

/* ── Activation ──────────────────────────────────────────────────────────── */

typedef struct { GtkWidget *window; char *action; char *param; } DeferredAction;

static gboolean fire_deferred_action(gpointer user_data)
{
    DeferredAction *d = user_data;
    if (d->param)
        gtk_widget_activate_action(d->window, d->action, "s", d->param);
    else
        gtk_widget_activate_action(d->window, d->action, NULL);
    g_free(d->action);
    g_free(d->param);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void activate_selected(PaletteCtx *ctx, gboolean keep_open)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(ctx->list);
    if (!row) return;
    const char *action = g_object_get_data(G_OBJECT(row), "cmd-action");
    const char *param  = g_object_get_data(G_OBJECT(row), "cmd-param");
    if (!action) return;

    /* Copy strings — the row (and its data) is owned by the dialog and
     * may be freed during the close animation. */
    DeferredAction *d = g_new(DeferredAction, 1);
    d->window = ctx->window;
    d->action = g_strdup(action);
    d->param  = g_strdup(param); /* g_strdup(NULL) → NULL */

    if (!keep_open)
        adw_dialog_close(ctx->dialog);
    /* Defer activation to the next idle iteration so the dialog finishes
     * releasing focus before the action returns it to the editor — prevents
     * GTK active-state accounting warnings up the widget tree. */
    g_idle_add(fire_deferred_action, d);
}

/* ── Callbacks ───────────────────────────────────────────────────────────── */

static void on_search_changed(GtkEditable *e, gpointer user_data)
{
    PaletteCtx *ctx = user_data;
    search_text = gtk_editable_get_text(e);
    gtk_list_box_invalidate_filter(ctx->list);
    gtk_list_box_invalidate_headers(ctx->list);

    /* Auto-select the first visible row and refresh the preview pane.
     * Re-selecting the already-selected row doesn't fire row-selected, so
     * we update the preview directly here (and clear it when nothing matches). */
    GtkListBoxRow *first_visible = NULL;
    for (int i = 0; ; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(ctx->list, i);
        if (!row) break;
        if (gtk_widget_get_visible(GTK_WIDGET(row))) {
            first_visible = row;
            break;
        }
    }
    if (first_visible) {
        gtk_list_box_select_row(ctx->list, first_visible);
        update_preview(first_visible, ctx);
    } else {
        gtk_list_box_unselect_all(ctx->list);
        update_preview(NULL, ctx);
    }
}

static void on_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
    (void)list; (void)row;
    activate_selected((PaletteCtx *)user_data, FALSE);
}

static void on_row_selected(GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
    (void)list;
    update_preview(row, (PaletteCtx *)user_data);
}

static gboolean on_search_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                      GdkModifierType state, gpointer user_data)
{
    (void)ctrl; (void)keycode; (void)state;
    PaletteCtx *ctx = user_data;

    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        /* Shift+Enter: activate the highlighted row but keep the palette
         * open, so users can chain multiple insertions (handy for symbols). */
        gboolean keep_open = (state & GDK_SHIFT_MASK) != 0;
        activate_selected(ctx, keep_open);
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Down) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(ctx->list);
        int idx = sel ? gtk_list_box_row_get_index(sel) + 1 : 0;
        for (;; idx++) {
            GtkListBoxRow *next = gtk_list_box_get_row_at_index(ctx->list, idx);
            if (!next) break;
            if (gtk_widget_get_visible(GTK_WIDGET(next))) {
                gtk_list_box_select_row(ctx->list, next);
                break;
            }
        }
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Up) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(ctx->list);
        int idx = sel ? gtk_list_box_row_get_index(sel) - 1 : -1;
        for (; idx >= 0; idx--) {
            GtkListBoxRow *prev = gtk_list_box_get_row_at_index(ctx->list, idx);
            if (!prev) break;
            if (gtk_widget_get_visible(GTK_WIDGET(prev))) {
                gtk_list_box_select_row(ctx->list, prev);
                break;
            }
        }
        return GDK_EVENT_STOP;
    }
    if (keyval == GDK_KEY_Escape) {
        adw_dialog_close(ctx->dialog);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

/* ── Row builder ─────────────────────────────────────────────────────────── */

static GtkWidget *build_row(const CmdEntry *e, int category)
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
        GtkWidget *sc = gtk_label_new(e->shortcut);
        gtk_widget_add_css_class(sc, "dim-label");
        gtk_widget_add_css_class(sc, "caption");
        gtk_box_append(GTK_BOX(row_box), sc);
    }

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
    g_object_set_data(G_OBJECT(row), "cmd-label",       (gpointer)e->label);
    g_object_set_data(G_OBJECT(row), "cmd-action",      (gpointer)e->action);
    g_object_set_data(G_OBJECT(row), "cmd-shortcut",    (gpointer)e->shortcut);
    g_object_set_data(G_OBJECT(row), "cmd-param",       (gpointer)e->param);
    g_object_set_data(G_OBJECT(row), "cmd-hint",        (gpointer)e->hint);
    g_object_set_data(G_OBJECT(row), "cmd-description", (gpointer)e->description);
    g_object_set_data(G_OBJECT(row), "cmd-category",    GINT_TO_POINTER(category));
    return row;
}

/* ── Public entry point ──────────────────────────────────────────────────── */

void silktex_cmd_palette_show(GtkWidget *window)
{
    search_text = NULL;

    AdwDialog *dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, _("Command Palette"));
    adw_dialog_set_content_width(dialog, 720);
    adw_dialog_set_content_height(dialog, 460);

    /* ── Top-level vertical box ─────────────────────────────────────────── */
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Search entry spans full width */
    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_set_margin_top(search, 8);
    gtk_widget_set_margin_bottom(search, 8);
    gtk_widget_set_margin_start(search, 8);
    gtk_widget_set_margin_end(search, 8);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search),
                                          _("Search commands and symbols…"));
    gtk_box_append(GTK_BOX(root), search);

    GtkWidget *hsep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(root), hsep);

    /* ── Horizontal split: list | separator | preview ───────────────────── */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_append(GTK_BOX(root), hbox);

    /* Left: scrollable list */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 320, -1);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), FALSE);
    gtk_widget_add_css_class(list, "navigation-sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_box_append(GTK_BOX(hbox), scroll);

    GtkWidget *vsep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(hbox), vsep);

    /* Right: preview pane */
    GtkWidget *preview_pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(preview_pane, TRUE);
    gtk_widget_set_vexpand(preview_pane, TRUE);
    gtk_widget_set_margin_top(preview_pane, 16);
    gtk_widget_set_margin_bottom(preview_pane, 16);
    gtk_widget_set_margin_start(preview_pane, 16);
    gtk_widget_set_margin_end(preview_pane, 16);
    gtk_box_append(GTK_BOX(hbox), preview_pane);

    /* Empty state placeholder */
    GtkWidget *preview_empty = gtk_label_new(_("Select a command or symbol to preview"));
    gtk_label_set_wrap(GTK_LABEL(preview_empty), TRUE);
    gtk_label_set_justify(GTK_LABEL(preview_empty), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(preview_empty, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(preview_empty, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(preview_empty, TRUE);
    gtk_widget_add_css_class(preview_empty, "dim-label");
    gtk_box_append(GTK_BOX(preview_pane), preview_empty);

    /* Preview content (hidden until a row is selected) */
    GtkWidget *preview_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_visible(preview_content, FALSE);
    gtk_box_append(GTK_BOX(preview_pane), preview_content);

    /* Glyph / shortcut display box */
    GtkWidget *glyph_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(glyph_box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(glyph_box, "card");
    gtk_widget_set_size_request(glyph_box, 52, 40);
    gtk_widget_set_margin_bottom(glyph_box, 4);
    gtk_box_append(GTK_BOX(preview_content), glyph_box);

    GtkWidget *preview_glyph = gtk_label_new("");
    gtk_widget_set_halign(preview_glyph, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(preview_glyph, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(GTK_WIDGET(preview_glyph), TRUE);
    gtk_widget_add_css_class(preview_glyph, "title-2");
    gtk_box_append(GTK_BOX(glyph_box), preview_glyph);

    /* Title label */
    GtkWidget *preview_title = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(preview_title), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(preview_title), TRUE);
    gtk_widget_add_css_class(preview_title, "heading");
    gtk_widget_add_css_class(preview_title, "monospace");
    gtk_box_append(GTK_BOX(preview_content), preview_title);

    /* Description label */
    GtkWidget *preview_desc = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(preview_desc), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(preview_desc), TRUE);
    gtk_widget_add_css_class(preview_desc, "dim-label");
    gtk_widget_add_css_class(preview_desc, "caption");
    gtk_box_append(GTK_BOX(preview_content), preview_desc);

    /* Code preview (LaTeX template) in a card */
    GtkWidget *code_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(code_card, "card");
    gtk_widget_set_margin_top(code_card, 4);

    GtkWidget *code_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(code_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(code_scroll, TRUE);
    gtk_box_append(GTK_BOX(code_card), code_scroll);

    GtkWidget *preview_code = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(preview_code), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(preview_code), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(preview_code), TRUE);
    gtk_widget_add_css_class(preview_code, "monospace");
    gtk_widget_set_margin_top(preview_code, 8);
    gtk_widget_set_margin_bottom(preview_code, 8);
    gtk_widget_set_margin_start(preview_code, 10);
    gtk_widget_set_margin_end(preview_code, 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(code_scroll), preview_code);
    gtk_box_append(GTK_BOX(preview_content), code_card);

    /* Allocate context (owned by dialog via g_object_set_data_full) */
    PaletteCtx *ctx = g_new(PaletteCtx, 1);
    ctx->dialog             = dialog;
    ctx->window             = window;
    ctx->list               = GTK_LIST_BOX(list);
    ctx->preview_glyph      = GTK_LABEL(preview_glyph);
    ctx->preview_glyph_box  = glyph_box;
    ctx->preview_title      = GTK_LABEL(preview_title);
    ctx->preview_desc       = GTK_LABEL(preview_desc);
    ctx->preview_content    = preview_content;
    ctx->preview_empty      = preview_empty;
    ctx->preview_code_scroll = code_card;
    ctx->preview_code       = GTK_LABEL(preview_code);
    g_object_set_data_full(G_OBJECT(dialog), "ctx", ctx, g_free);

    /* Populate the list */
    for (gsize i = 0; i < G_N_ELEMENTS(CMD_ENTRIES); i++)
        gtk_list_box_append(GTK_LIST_BOX(list), build_row(&CMD_ENTRIES[i], CAT_COMMAND));
    for (gsize i = 0; i < G_N_ELEMENTS(SYMBOL_ENTRIES); i++)
        gtk_list_box_append(GTK_LIST_BOX(list), build_row(&SYMBOL_ENTRIES[i], CAT_LATEX));

    gtk_list_box_set_filter_func(GTK_LIST_BOX(list), filter_row, NULL, NULL);
    gtk_list_box_set_header_func(GTK_LIST_BOX(list), update_header_func, NULL, NULL);

    /* Select first row and fire preview */
    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0);
    if (first) {
        gtk_list_box_select_row(GTK_LIST_BOX(list), first);
        update_preview(first, ctx);
    }

    /* Connect signals */
    g_signal_connect(search, "search-changed", G_CALLBACK(on_search_changed), ctx);
    g_signal_connect(list,   "row-activated",  G_CALLBACK(on_row_activated),  ctx);
    g_signal_connect(list,   "row-selected",   G_CALLBACK(on_row_selected),   ctx);

    GtkEventControllerKey *key_ctrl =
        GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key_ctrl),
                                               GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(search, GTK_EVENT_CONTROLLER(key_ctrl));
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_search_key_pressed), ctx);

    adw_dialog_set_child(dialog, root);
    adw_dialog_present(dialog, window);
    gtk_widget_grab_focus(search);
}
