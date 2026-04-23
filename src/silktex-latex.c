/*
 * SilkTex - LaTeX insertion helpers
 * SPDX-License-Identifier: MIT
 */
#include "silktex-latex.h"
#include <glib/gi18n.h>
#include <string.h>

/* ------------------------------------------------------------------ table */

static const char align_chars[][2]   = { "l", "c", "r" };
static const char bracket_names[][16]= {
    "matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix"
};

char *
silktex_latex_generate_table(int rows, int cols, int borders, int alignment)
{
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    if (alignment < 0 || alignment > 2) alignment = 1;
    if (borders  < 0 || borders  > 2) borders  = 0;

    GString *col_spec = g_string_new("{");
    if (borders) g_string_append(col_spec, "|");
    for (int j = 0; j < cols; j++) {
        g_string_append(col_spec, align_chars[alignment]);
        if (borders == 2 || (borders == 1 && j == cols - 1))
            g_string_append(col_spec, "|");
    }
    g_string_append(col_spec, "}");

    GString *body = g_string_new(NULL);
    if (borders) g_string_append(body, "\n\\hline");
    for (int i = 0; i < rows; i++) {
        g_string_append(body, "\n\t");
        for (int j = 0; j < cols; j++) {
            g_string_append_printf(body, "%d%d", i + 1, j + 1);
            g_string_append(body, j != cols - 1 ? " & " : "\\\\");
        }
        if (borders == 2 || (borders == 1 && i == rows - 1))
            g_string_append(body, "\n\\hline");
    }

    char *out = g_strdup_printf("\\begin{tabular}%s%s\n\\end{tabular}\n",
                                col_spec->str, body->str);
    g_string_free(col_spec, TRUE);
    g_string_free(body, TRUE);
    return out;
}

char *
silktex_latex_generate_matrix(int bracket, int rows, int cols)
{
    if (bracket < 0 || bracket >= (int)G_N_ELEMENTS(bracket_names)) bracket = 1;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;

    GString *s = g_string_new(NULL);
    g_string_append_printf(s, "$\\begin{%s}", bracket_names[bracket]);
    for (int i = 0; i < rows; i++) {
        g_string_append(s, "\n\t");
        for (int j = 0; j < cols; j++) {
            g_string_append_printf(s, "%d%d", i + 1, j + 1);
            g_string_append(s, j != cols - 1 ? " & " : "\\\\");
        }
    }
    g_string_append_printf(s, "\n\\end{%s}$\n", bracket_names[bracket]);
    return g_string_free(s, FALSE);
}

char *
silktex_latex_generate_image(const char *path, const char *caption,
                             const char *label, double scale)
{
    if (scale <= 0.0) scale = 1.0;
    g_autofree char *scale_str = g_strdup_printf("%.2f", scale);
    /* replace decimal comma with period for LaTeX */
    for (char *p = scale_str; *p; p++) if (*p == ',') *p = '.';

    return g_strdup_printf(
        "\\begin{figure}[htp]\n"
        "\\centering\n"
        "\\includegraphics[scale=%s]{%s}\n"
        "\\caption{%s}\n"
        "\\label{%s}\n"
        "\\end{figure}\n",
        scale_str,
        path    ? path    : "",
        caption ? caption : "",
        label   ? label   : "");
}

/* ------------------------------------------------------------------ insert */

void
silktex_latex_insert_at_cursor(SilktexEditor *editor,
                               const char    *before,
                               const char    *after)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(editor));
    if (!before) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(editor);
    GtkTextBuffer   *buf  = GTK_TEXT_BUFFER(sbuf);

    GtkTextIter s, e;
    gboolean have_sel = gtk_text_buffer_get_selection_bounds(buf, &s, &e);

    gtk_text_buffer_begin_user_action(buf);
    if (have_sel && after) {
        g_autofree char *text = gtk_text_iter_get_text(&s, &e);
        g_autofree char *wrapped = g_strdup_printf("%s%s%s", before, text, after);
        gtk_text_buffer_delete(buf, &s, &e);
        gtk_text_buffer_insert(buf, &s, wrapped, -1);
    } else {
        GtkTextIter insert;
        GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
        gtk_text_buffer_get_iter_at_mark(buf, &insert, mark);
        gtk_text_buffer_insert(buf, &insert, before, -1);
        if (after) {
            /* Remember position between before/after */
            GtkTextMark *between = gtk_text_buffer_create_mark(buf, NULL, &insert, TRUE);
            gtk_text_buffer_insert(buf, &insert, after, -1);
            GtkTextIter mid;
            gtk_text_buffer_get_iter_at_mark(buf, &mid, between);
            gtk_text_buffer_place_cursor(buf, &mid);
            gtk_text_buffer_delete_mark(buf, between);
        }
    }
    gtk_text_buffer_end_user_action(buf);
    silktex_editor_scroll_to_cursor(editor);
}

void
silktex_latex_insert_structure(SilktexEditor *editor, const char *command)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(editor));
    g_autofree char *before = g_strdup_printf("\\%s{", command);
    silktex_latex_insert_at_cursor(editor, before, "}\n");
}

void
silktex_latex_insert_environment(SilktexEditor *editor, const char *env)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(editor));
    g_autofree char *before = g_strdup_printf("\\begin{%s}\n\t", env);
    g_autofree char *after  = g_strdup_printf("\n\\end{%s}\n", env);
    silktex_latex_insert_at_cursor(editor, before, after);
}

/* ============================================================== dialogs === */

typedef struct {
    SilktexEditor *editor;
    GtkWidget     *dialog;
    GtkWidget     *file_entry;
    GtkWidget     *caption_entry;
    GtkWidget     *label_entry;
    GtkWidget     *scale_spin;
} ImgCtx;

static char *
relativize(const char *file, SilktexEditor *editor)
{
    const char *base = silktex_editor_get_filename(editor);
    if (!base) return g_strdup(file);
    g_autofree char *dir = g_path_get_dirname(base);
    GFile *root = g_file_new_for_path(dir);
    GFile *target = g_file_new_for_path(file);
    char *rel = g_file_get_relative_path(root, target);
    g_object_unref(root);
    g_object_unref(target);
    return rel ? rel : g_strdup(file);
}

static void
img_pick_response(GObject *src, GAsyncResult *res, gpointer ud)
{
    ImgCtx *ctx = ud;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (f) {
        char *p = g_file_get_path(f);
        gtk_editable_set_text(GTK_EDITABLE(ctx->file_entry), p ? p : "");
        g_free(p);
        g_object_unref(f);
    }
}

static void
on_img_browse(GtkButton *b, gpointer ud)
{
    ImgCtx *ctx = ud;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, _("Select Image"));
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, _("Images"));
    gtk_file_filter_add_mime_type(flt, "image/*");
    gtk_file_filter_add_pattern(flt, "*.png");
    gtk_file_filter_add_pattern(flt, "*.jpg");
    gtk_file_filter_add_pattern(flt, "*.jpeg");
    gtk_file_filter_add_pattern(flt, "*.pdf");
    gtk_file_filter_add_pattern(flt, "*.eps");
    gtk_file_filter_add_pattern(flt, "*.svg");
    GListStore *ls = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(ls, flt);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(ls));
    gtk_file_dialog_set_default_filter(d, flt);
    gtk_file_dialog_open(d, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(b))),
                          NULL, img_pick_response, ctx);
    g_object_unref(ls);
    g_object_unref(flt);
}

static void
on_img_response(AdwAlertDialog *dlg, const char *resp, gpointer ud)
{
    ImgCtx *ctx = ud;
    if (g_strcmp0(resp, "apply") == 0 && ctx->editor) {
        const char *file = gtk_editable_get_text(GTK_EDITABLE(ctx->file_entry));
        if (file && *file) {
            g_autofree char *rel = relativize(file, ctx->editor);
            const char *cap = gtk_editable_get_text(GTK_EDITABLE(ctx->caption_entry));
            const char *lbl = gtk_editable_get_text(GTK_EDITABLE(ctx->label_entry));
            double s = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ctx->scale_spin));
            g_autofree char *latex = silktex_latex_generate_image(rel, cap, lbl, s);
            silktex_editor_insert_package(ctx->editor, "graphicx", NULL);
            silktex_latex_insert_at_cursor(ctx->editor, latex, NULL);
        }
    }
    g_free(ctx);
}

void
silktex_latex_insert_image_dialog(GtkWindow *parent, SilktexEditor *editor)
{
    g_return_if_fail(editor);

    ImgCtx *ctx = g_new0(ImgCtx, 1);
    ctx->editor = editor;

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(
        adw_alert_dialog_new(_("Insert Image"), NULL));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "apply",  _("Insert"));
    adw_alert_dialog_set_response_appearance(dlg, "apply", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "apply");

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);

    AdwEntryRow *row_file = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_file), _("File Path"));
    GtkWidget *btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    adw_entry_row_add_suffix(row_file, btn);
    ctx->file_entry = GTK_WIDGET(row_file);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_img_browse), ctx);

    AdwEntryRow *row_cap = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_cap), _("Caption"));
    ctx->caption_entry = GTK_WIDGET(row_cap);

    AdwEntryRow *row_lbl = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_lbl), _("Label"));
    ctx->label_entry = GTK_WIDGET(row_lbl);

    GtkAdjustment *adj = gtk_adjustment_new(1.0, 0.1, 5.0, 0.05, 0.1, 0);
    AdwSpinRow *row_scale = ADW_SPIN_ROW(adw_spin_row_new(adj, 0.05, 2));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_scale), _("Scale"));
    ctx->scale_spin = GTK_WIDGET(row_scale);

    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(row_file));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(row_cap));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(row_lbl));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(row_scale));

    adw_alert_dialog_set_extra_child(dlg, list);
    g_signal_connect(dlg, "response", G_CALLBACK(on_img_response), ctx);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(parent));
}

/* --------------------------------------------------------------- table */

typedef struct {
    SilktexEditor *editor;
    AdwSpinRow    *rows;
    AdwSpinRow    *cols;
    AdwComboRow   *border;
    AdwComboRow   *align;
} TblCtx;

static void
on_tbl_response(AdwAlertDialog *dlg, const char *resp, gpointer ud)
{
    TblCtx *ctx = ud;
    if (g_strcmp0(resp, "apply") == 0 && ctx->editor) {
        int r = (int)adw_spin_row_get_value(ctx->rows);
        int c = (int)adw_spin_row_get_value(ctx->cols);
        int b = (int)adw_combo_row_get_selected(ctx->border);
        int a = (int)adw_combo_row_get_selected(ctx->align);
        g_autofree char *latex = silktex_latex_generate_table(r, c, b, a);
        silktex_latex_insert_at_cursor(ctx->editor, latex, NULL);
    }
    g_free(ctx);
}

void
silktex_latex_insert_table_dialog(GtkWindow *parent, SilktexEditor *editor)
{
    g_return_if_fail(editor);
    TblCtx *ctx = g_new0(TblCtx, 1);
    ctx->editor = editor;

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(
        adw_alert_dialog_new(_("Insert Table"), NULL));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "apply",  _("Insert"));
    adw_alert_dialog_set_response_appearance(dlg, "apply", ADW_RESPONSE_SUGGESTED);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);

    ctx->rows = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 64, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->rows), _("Rows"));
    adw_spin_row_set_value(ctx->rows, 3);

    ctx->cols = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 16, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->cols), _("Columns"));
    adw_spin_row_set_value(ctx->cols, 3);

    GtkStringList *b_model = gtk_string_list_new(
        (const char *[]){ _("No borders"), _("Outer border"), _("All borders"), NULL });
    ctx->border = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->border), _("Borders"));
    adw_combo_row_set_model(ctx->border, G_LIST_MODEL(b_model));

    GtkStringList *a_model = gtk_string_list_new(
        (const char *[]){ _("Left"), _("Center"), _("Right"), NULL });
    ctx->align = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->align), _("Alignment"));
    adw_combo_row_set_model(ctx->align, G_LIST_MODEL(a_model));
    adw_combo_row_set_selected(ctx->align, 1);

    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->rows));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->cols));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->border));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->align));

    adw_alert_dialog_set_extra_child(dlg, list);
    g_signal_connect(dlg, "response", G_CALLBACK(on_tbl_response), ctx);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(parent));
}

/* -------------------------------------------------------------- matrix */

typedef struct {
    SilktexEditor *editor;
    AdwSpinRow    *rows;
    AdwSpinRow    *cols;
    AdwComboRow   *bracket;
} MtxCtx;

static void
on_mtx_response(AdwAlertDialog *dlg, const char *resp, gpointer ud)
{
    MtxCtx *ctx = ud;
    if (g_strcmp0(resp, "apply") == 0 && ctx->editor) {
        int r = (int)adw_spin_row_get_value(ctx->rows);
        int c = (int)adw_spin_row_get_value(ctx->cols);
        int b = (int)adw_combo_row_get_selected(ctx->bracket);
        g_autofree char *latex = silktex_latex_generate_matrix(b, r, c);
        silktex_editor_insert_package(ctx->editor, "amsmath", NULL);
        silktex_latex_insert_at_cursor(ctx->editor, latex, NULL);
    }
    g_free(ctx);
}

void
silktex_latex_insert_matrix_dialog(GtkWindow *parent, SilktexEditor *editor)
{
    g_return_if_fail(editor);
    MtxCtx *ctx = g_new0(MtxCtx, 1);
    ctx->editor = editor;

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(
        adw_alert_dialog_new(_("Insert Matrix"), NULL));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "apply",  _("Insert"));
    adw_alert_dialog_set_response_appearance(dlg, "apply", ADW_RESPONSE_SUGGESTED);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);

    ctx->rows = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 16, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->rows), _("Rows"));
    adw_spin_row_set_value(ctx->rows, 3);

    ctx->cols = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 16, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->cols), _("Columns"));
    adw_spin_row_set_value(ctx->cols, 3);

    GtkStringList *b_model = gtk_string_list_new((const char *[]){
        "matrix", "pmatrix (())", "bmatrix ([])",
        "Bmatrix ({})", "vmatrix (||)", "Vmatrix (‖‖)", NULL });
    ctx->bracket = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->bracket), _("Bracket"));
    adw_combo_row_set_model(ctx->bracket, G_LIST_MODEL(b_model));
    adw_combo_row_set_selected(ctx->bracket, 1);

    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->rows));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->cols));
    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->bracket));

    adw_alert_dialog_set_extra_child(dlg, list);
    g_signal_connect(dlg, "response", G_CALLBACK(on_mtx_response), ctx);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(parent));
}

/* ------------------------------------------------------------- biblio */

typedef struct {
    SilktexEditor *editor;
    AdwEntryRow   *file_row;
} BibCtx;

static void
bib_pick_response(GObject *src, GAsyncResult *res, gpointer ud)
{
    BibCtx *ctx = ud;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (f) {
        char *p = g_file_get_path(f);
        gtk_editable_set_text(GTK_EDITABLE(ctx->file_row), p ? p : "");
        g_free(p);
        g_object_unref(f);
    }
}

static void
on_bib_browse(GtkButton *b, gpointer ud)
{
    BibCtx *ctx = ud;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, _("Select BibTeX File"));
    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, _("BibTeX files"));
    gtk_file_filter_add_pattern(flt, "*.bib");
    GListStore *ls = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(ls, flt);
    gtk_file_dialog_set_filters(d, G_LIST_MODEL(ls));
    gtk_file_dialog_set_default_filter(d, flt);
    gtk_file_dialog_open(d, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(b))),
                         NULL, bib_pick_response, ctx);
    g_object_unref(ls);
    g_object_unref(flt);
}

static void
on_bib_response(AdwAlertDialog *dlg, const char *resp, gpointer ud)
{
    BibCtx *ctx = ud;
    if (g_strcmp0(resp, "apply") == 0 && ctx->editor) {
        const char *file = gtk_editable_get_text(GTK_EDITABLE(ctx->file_row));
        if (file && *file) {
            g_autofree char *rel = relativize(file, ctx->editor);
            /* strip .bib extension if present */
            char *base = g_strdup(rel);
            char *dot = g_strrstr(base, ".bib");
            if (dot) *dot = '\0';
            g_autofree char *snippet = g_strdup_printf(
                "\\bibliographystyle{plain}\n\\bibliography{%s}\n", base);
            silktex_latex_insert_at_cursor(ctx->editor, snippet, NULL);
            g_free(base);
        }
    }
    g_free(ctx);
}

void
silktex_latex_insert_biblio_dialog(GtkWindow *parent, SilktexEditor *editor)
{
    g_return_if_fail(editor);

    BibCtx *ctx = g_new0(BibCtx, 1);
    ctx->editor = editor;

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(
        adw_alert_dialog_new(_("Insert Bibliography"),
                             _("Insert \\bibliography command pointing to a .bib file.")));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "apply",  _("Insert"));
    adw_alert_dialog_set_response_appearance(dlg, "apply", ADW_RESPONSE_SUGGESTED);

    GtkWidget *list = gtk_list_box_new();
    gtk_widget_add_css_class(list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);

    ctx->file_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ctx->file_row), _("BibTeX File"));
    GtkWidget *btn = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    adw_entry_row_add_suffix(ctx->file_row, btn);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_bib_browse), ctx);

    gtk_list_box_append(GTK_LIST_BOX(list), GTK_WIDGET(ctx->file_row));
    adw_alert_dialog_set_extra_child(dlg, list);
    g_signal_connect(dlg, "response", G_CALLBACK(on_bib_response), ctx);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(parent));
}
