/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window.h"
#include "prefs.h"
#include "searchbar.h"
#include "snippets.h"
#include "synctex.h"
#include "structure.h"
#include "latex.h"
#include "git.h"
#include "configfile.h"
#include "constants.h"
#include "utils.h"
#include "i18n.h"
#include <glib/gstdio.h>
#include <gio/gio.h>

/* Minimum size request for the editor pane (allows ~50% split on typical window widths). */
#define EDITOR_MIN_WIDTH 280
/* Reserve space for the PDF preview when enforcing paned position. */
#define PREVIEW_PANE_MIN_WIDTH 200

struct _SilktexWindow {
    AdwApplicationWindow parent_instance;

    AdwToolbarView *root_toolbar_view;
    AdwWindowTitle *window_title;
    AdwToastOverlay *toast_overlay;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    AdwOverlaySplitView *split_view;
    GtkPaned *editor_paned;
    AdwToolbarView *editor_toolbar_view;
    GtkBox *editor_bottom_bar;
    AdwToolbarView *preview_toolbar_view;
    GtkBox *preview_box;
    GtkBox *structure_container;
    GtkLabel *page_label;
    GtkLabel *preview_status;
    GtkToggleButton *btn_preview;
    GtkToggleButton *btn_sidebar;
    GtkButton *btn_compile;
    GtkMenuButton *btn_menu;

    SilktexPreview *preview;
    SilktexCompiler *compiler;
    SilktexSearchbar *searchbar;
    SilktexSnippets *snippets;
    SilktexStructure *structure;

    /* compile log panel */
    GtkRevealer *log_revealer;
    GtkTextBuffer *log_buf;
    GtkToggleButton *log_toggle;
    GtkWidget *log_text_view;

    /* graphical theme selector in the primary popover */
    GtkToggleButton *theme_follow;
    GtkToggleButton *theme_light;
    GtkToggleButton *theme_dark;

    /* Git integration */
    SilktexGitStatus *git_status;
    char *git_status_message;
    AdwDialog *git_dialog;
    GtkLabel *git_branch_label;
    GtkLabel *git_repo_label;
    GtkListBox *git_list;
    GtkEditable *git_commit_message;

    guint compile_timer_id;
    guint autosave_timer_id;
    gboolean auto_compile;
    gboolean is_fullscreen;
    gboolean preview_narrow;
    gboolean preview_auto_collapsed;

    /* Last horizontal split between editor and PDF preview; remembered across hide/show. */
    gint preview_pane_pos;
    gboolean preview_pane_restorable;
    gboolean preview_pane_silence; /* during programmatic set_position to avoid re-entrancy */
    gboolean preview_split_seeded; /* FALSE until first apply (50% or restore) — ignore Gtk defaults */

    AdwToast *current_toast;
};

G_DEFINE_FINAL_TYPE (SilktexWindow, silktex_window, ADW_TYPE_APPLICATION_WINDOW)

    static void on_prefs_apply(gpointer user_data);
static void refresh_git_state(SilktexWindow *self);
static void update_git_dialog(SilktexWindow *self);
static void update_git_actions(SilktexWindow *self);
static void apply_editor_paned_half_split(SilktexWindow *self);

/* ------------------------------------------------------------ helpers === */

static SilktexEditor *get_editor_for_page(AdwTabPage *page)
{
    if (page == NULL) return NULL;
    GtkWidget *child = adw_tab_page_get_child(page);
    if (child == NULL) return NULL;
    return g_object_get_data(G_OBJECT(child), "silktex-editor");
}

static void update_window_title(SilktexWindow *self)
{
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        g_autofree char *basename = silktex_editor_get_basename(editor);
        const char *modified = silktex_editor_get_modified(editor) ? "*" : "";
        g_autofree char *title = g_strdup_printf("%s%s", modified, basename);
        adw_window_title_set_subtitle(self->window_title, title);
    } else {
        adw_window_title_set_subtitle(self->window_title, "");
    }
}

static void update_tab_title(SilktexWindow *self, AdwTabPage *page, SilktexEditor *editor)
{
    g_autofree char *basename = silktex_editor_get_basename(editor);
    const char *modified = silktex_editor_get_modified(editor) ? "*" : "";
    g_autofree char *title = g_strdup_printf("%s%s", modified, basename);
    adw_tab_page_set_title(page, title);
}

static void update_page_label(SilktexWindow *self)
{
    if (!self->page_label) return;
    int page = silktex_preview_get_page(self->preview) + 1;
    int total = silktex_preview_get_n_pages(self->preview);
    if (total <= 0) {
        gtk_label_set_label(self->page_label, "—");
        return;
    }
    g_autofree char *s = g_strdup_printf("%d / %d", page, total);
    gtk_label_set_label(self->page_label, s);
}

static void update_log_panel(SilktexWindow *self)
{
    if (!self->log_buf) return;
    const char *log = silktex_compiler_get_log(self->compiler);
    gtk_text_buffer_set_text(self->log_buf, log ? log : "", -1);
}

static const char *effective_editor_scheme(void)
{
    const char *mode = config_get_string("Interface", "theme");
    if (g_strcmp0(mode, "dark") == 0) return "Adwaita-dark";
    if (g_strcmp0(mode, "light") == 0) return "Adwaita";

    AdwStyleManager *sm = adw_style_manager_get_default();
    return adw_style_manager_get_dark(sm) ? "Adwaita-dark" : "Adwaita";
}

static void apply_theme_to_editor(SilktexEditor *editor)
{
    if (editor != NULL) {
        silktex_editor_set_style_scheme(editor, effective_editor_scheme());
    }
}

static void apply_theme_to_all_editors(SilktexWindow *self)
{
    guint n = adw_tab_view_get_n_pages(self->tab_view);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(self->tab_view, i);
        apply_theme_to_editor(get_editor_for_page(page));
    }
}

static void focus_active_editor(SilktexWindow *self)
{
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        gtk_widget_grab_focus(silktex_editor_get_view(editor));
    }
}

static void add_to_recent(GFile *file)
{
    if (!file) return;
    GtkRecentManager *mgr = gtk_recent_manager_get_default();
    char *uri = g_file_get_uri(file);
    if (uri) {
        gtk_recent_manager_add_item(mgr, uri);
        g_free(uri);
    }
}

/* ------------------------------------------------------------ timers === */

static gboolean on_compile_timer(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    self->compile_timer_id = 0;

    if (self->auto_compile) {
        SilktexEditor *editor = silktex_window_get_active_editor(self);
        if (editor != NULL) {
            /* Snapshot the buffer on the UI thread; the compile worker is
             * forbidden from touching GtkTextBuffer. */
            silktex_editor_update_workfile(editor);
            silktex_compiler_request_compile(self->compiler, editor);
        }
    }

    return G_SOURCE_REMOVE;
}

static void restart_compile_timer(SilktexWindow *self)
{
    if (self->compile_timer_id > 0) {
        g_source_remove(self->compile_timer_id);
        self->compile_timer_id = 0;
    }
    int delay = config_get_integer("Compile", "timer");
    if (delay < 1) delay = 1;
    self->compile_timer_id = g_timeout_add_seconds((guint)delay, on_compile_timer, self);
}

static gboolean on_autosave_timer(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    guint n = adw_tab_view_get_n_pages(self->tab_view);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(self->tab_view, i);
        SilktexEditor *editor = get_editor_for_page(page);
        if (editor && silktex_editor_get_modified(editor)) {
            const char *fname = silktex_editor_get_filename(editor);
            if (fname && *fname) {
                GFile *f = g_file_new_for_path(fname);
                silktex_editor_save_file(editor, f, NULL);
                g_object_unref(f);
            }
        }
    }
    refresh_git_state(self);
    return G_SOURCE_CONTINUE;
}

static void restart_autosave_timer(SilktexWindow *self)
{
    if (self->autosave_timer_id > 0) {
        g_source_remove(self->autosave_timer_id);
        self->autosave_timer_id = 0;
    }
    if (config_get_boolean("File", "autosaving")) {
        int delay = config_get_integer("File", "autosave_timer");
        if (delay < 1) delay = 1;
        self->autosave_timer_id = g_timeout_add_seconds((guint)delay * 60, on_autosave_timer, self);
    }
}

/* ------------------------------------------------------------ signals === */

static void on_editor_changed(SilktexEditor *editor, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    if (page != NULL && get_editor_for_page(page) == editor) {
        update_tab_title(self, page, editor);
        update_window_title(self);
    }

    if (self->auto_compile) {
        restart_compile_timer(self);
    }
}

static void on_compile_finished(SilktexCompiler *compiler, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        const char *pdffile = silktex_editor_get_pdffile(editor);
        if (pdffile != NULL && g_file_test(pdffile, G_FILE_TEST_EXISTS)) {
            silktex_preview_load_file(self->preview, pdffile);
        }
    }
    update_log_panel(self);
    if (self->preview_status) gtk_label_set_label(self->preview_status, _("Compiled"));
}

static void on_compile_error(SilktexCompiler *compiler, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_window_show_toast(self, _("Compilation error — see compile log"));
    update_log_panel(self);
    if (self->preview_status) gtk_label_set_label(self->preview_status, _("Compile error"));

    /*
     * The compiler restores the last-good PDF on failure, so if the
     * preview hasn't loaded anything yet (e.g. the user opened a file
     * whose first auto-compile failed) we still try to surface that
     * preserved PDF here.
     */
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        const char *pdffile = silktex_editor_get_pdffile(editor);
        if (pdffile != NULL && g_file_test(pdffile, G_FILE_TEST_EXISTS)) {
            silktex_preview_load_file(self->preview, pdffile);
        }
    }
}

static void on_preview_page_changed(GObject *p, GParamSpec *ps, gpointer ud)
{
    update_page_label(SILKTEX_WINDOW(ud));
}

/* ------------------------------------------------------------ pages === */

static GtkWidget *create_editor_page(SilktexWindow *self, SilktexEditor *editor)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), silktex_editor_get_view(editor));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    g_object_set_data_full(G_OBJECT(scrolled), "silktex-editor", g_object_ref(editor),
                           g_object_unref);

    g_signal_connect(editor, "changed", G_CALLBACK(on_editor_changed), self);

    silktex_editor_apply_settings(editor);
    apply_theme_to_editor(editor);

    return scrolled;
}

/* ------------------------------------------------------------ file actions */

static void action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    silktex_window_new_tab(SILKTEX_WINDOW(user_data));
}

static void on_open_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    if (file == NULL) {
        if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
            g_warning("Failed to open file: %s", error->message);
        }
        g_clear_error(&error);
        return;
    }

    silktex_window_open_file(self, file);
    g_object_unref(file);
}

static void action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Open LaTeX Document"));
    gtk_file_dialog_set_modal(dialog, TRUE);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("LaTeX files"));
    gtk_file_filter_add_pattern(filter, "*.tex");
    gtk_file_filter_add_pattern(filter, "*.ltx");
    gtk_file_filter_add_pattern(filter, "*.sty");
    gtk_file_filter_add_pattern(filter, "*.cls");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, filter);

    gtk_file_dialog_open(dialog, GTK_WINDOW(self), NULL, on_open_response, self);

    g_object_unref(filters);
    g_object_unref(filter);
}

static void on_save_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
            g_warning("Failed to save file: %s", error->message);
        }
        g_clear_error(&error);
        return;
    }

    /* Ensure the chosen file ends in a recognised LaTeX extension.
     * GtkFileDialog has no auto-suffix API, so users who type "mydoc"
     * would otherwise get an extensionless file that pdflatex can't
     * handle. */
    g_autofree char *picked_path = g_file_get_path(file);
    if (picked_path) {
        g_autofree char *lower = g_ascii_strdown(picked_path, -1);
        if (!g_str_has_suffix(lower, ".tex") && !g_str_has_suffix(lower, ".ltx") &&
            !g_str_has_suffix(lower, ".sty") && !g_str_has_suffix(lower, ".cls")) {
            g_autofree char *with_tex = g_strconcat(picked_path, ".tex", NULL);
            g_object_unref(file);
            file = g_file_new_for_path(with_tex);
        }
    }

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        GError *save_error = NULL;
        if (!silktex_editor_save_file(editor, file, &save_error)) {
            silktex_window_show_toast(self, save_error ? save_error->message : _("Save failed"));
            g_clear_error(&save_error);
        } else {
            AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
            update_tab_title(self, page, editor);
            update_window_title(self);
            add_to_recent(file);
            refresh_git_state(self);
            silktex_window_show_toast(self, _("File saved"));
        }
    }

    g_object_unref(file);
}

/*
 * Configure a GtkFileDialog for saving a .tex document.  We attach a
 * dedicated LaTeX filter (so the dialog's file-type picker defaults to
 * ".tex") and pre-fill the filename with a ".tex" extension — GtkFileDialog
 * does not provide an automatic suffix API, so this is the cleanest way
 * to ensure the user ends up with a .tex file.
 */
static void configure_tex_save_dialog(GtkFileDialog *dialog, SilktexEditor *editor)
{
    GtkFileFilter *tex_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(tex_filter, _("LaTeX (*.tex)"));
    gtk_file_filter_add_pattern(tex_filter, "*.tex");
    gtk_file_filter_add_pattern(tex_filter, "*.ltx");

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, _("All files"));
    gtk_file_filter_add_pattern(all_filter, "*");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, tex_filter);
    g_list_store_append(filters, all_filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, tex_filter);

    g_autofree char *suggested = NULL;
    if (editor) {
        g_autofree char *base = silktex_editor_get_basename(editor);
        if (base && *base) {
            if (g_str_has_suffix(base, ".tex") || g_str_has_suffix(base, ".ltx"))
                suggested = g_strdup(base);
            else
                suggested = g_strdup_printf("%s.tex", base);
        }
    }
    gtk_file_dialog_set_initial_name(dialog, suggested ? suggested : "untitled.tex");

    g_object_unref(filters);
    g_object_unref(tex_filter);
    g_object_unref(all_filter);
}

static void action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);

    if (editor == NULL) return;

    const char *filename = silktex_editor_get_filename(editor);
    if (filename != NULL) {
        GFile *file = g_file_new_for_path(filename);
        GError *error = NULL;
        if (!silktex_editor_save_file(editor, file, &error)) {
            silktex_window_show_toast(self, error ? error->message : _("Save failed"));
            g_clear_error(&error);
        } else {
            AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
            update_tab_title(self, page, editor);
            update_window_title(self);
            add_to_recent(file);
            refresh_git_state(self);
        }
        g_object_unref(file);
    } else {
        GtkFileDialog *dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, _("Save LaTeX Document"));
        gtk_file_dialog_set_modal(dialog, TRUE);
        configure_tex_save_dialog(dialog, editor);
        gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_save_response, self);
    }
}

static void action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Save LaTeX Document As"));
    gtk_file_dialog_set_modal(dialog, TRUE);
    configure_tex_save_dialog(dialog, editor);
    gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_save_response, self);
}

/* ------------------------------------------------------------ export --- */

static void on_export_response(GObject *source, GAsyncResult *res, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
    if (!file) return;

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) {
        g_object_unref(file);
        return;
    }

    const char *src_pdf = silktex_editor_get_pdffile(editor);
    if (!src_pdf || !g_file_test(src_pdf, G_FILE_TEST_EXISTS)) {
        silktex_window_show_toast(self, _("No PDF yet — compile first"));
        g_object_unref(file);
        return;
    }

    GFile *src = g_file_new_for_path(src_pdf);
    GError *err = NULL;
    if (!g_file_copy(src, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err)) {
        silktex_window_show_toast(self, err ? err->message : _("Export failed"));
        g_clear_error(&err);
    } else {
        silktex_window_show_toast(self, _("PDF exported"));
    }
    g_object_unref(src);
    g_object_unref(file);
}

static void action_export_pdf(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Export PDF"));
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor) {
        g_autofree char *base = silktex_editor_get_basename(editor);
        char *dot = base ? strrchr(base, '.') : NULL;
        if (dot) *dot = '\0';
        g_autofree char *name = g_strdup_printf("%s.pdf", base ? base : "document");
        gtk_file_dialog_set_initial_name(dlg, name);
    }
    gtk_file_dialog_save(dlg, GTK_WINDOW(self), NULL, on_export_response, self);
}

/* ------------------------------------------------------------ close --- */

static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    if (page != NULL) adw_tab_view_close_page(self->tab_view, page);
}

/* ------------------------------------------------------------ editing */

static void action_undo(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_undo(e);
}
static void action_redo(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_redo(e);
}

static void action_compile(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) {
        if (self->preview_status) gtk_label_set_label(self->preview_status, _("Compiling…"));
        silktex_editor_update_workfile(e);
        silktex_compiler_force_compile(self->compiler, e);
    }
}

static void action_bold(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_apply_textstyle(e, "bold");
}
static void action_italic(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_apply_textstyle(e, "italic");
}
static void action_underline(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_apply_textstyle(e, "underline");
}
static void action_align_left(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_apply_textstyle(e, "left");
}
static void action_align_center(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_apply_textstyle(e, "center");
}
static void action_align_right(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_apply_textstyle(e, "right");
}

/* ------------------------------------------------------------ insert */

static void action_insert_section(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_structure(e, "section");
}
static void action_insert_subsection(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_structure(e, "subsection");
}
static void action_insert_subsubsection(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_structure(e, "subsubsection");
}
static void action_insert_chapter(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_structure(e, "chapter");
}
static void action_insert_paragraph(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_structure(e, "paragraph");
}

static void action_insert_itemize(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_at_cursor(e, "\\begin{itemize}\n\t\\item ", "\n\\end{itemize}\n");
}
static void action_insert_enumerate(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e)
        silktex_latex_insert_at_cursor(e, "\\begin{enumerate}\n\t\\item ", "\n\\end{enumerate}\n");
}
static void action_insert_description(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e)
        silktex_latex_insert_at_cursor(e, "\\begin{description}\n\t\\item[term] ",
                                       "\n\\end{description}\n");
}
static void action_insert_equation(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_environment(e, "equation");
}
static void action_insert_quote(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_latex_insert_environment(e, "quote");
}

static void action_insert_image(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_latex_insert_image_dialog(GTK_WINDOW(self), e);
}
static void action_insert_table(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_latex_insert_table_dialog(GTK_WINDOW(self), e);
}
static void action_insert_matrix(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_latex_insert_matrix_dialog(GTK_WINDOW(self), e);
}
static void action_insert_biblio(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_latex_insert_biblio_dialog(GTK_WINDOW(self), e);
}

/* ------------------------------------------------------------ preview */

static void action_zoom_in(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_zoom_in(SILKTEX_WINDOW(ud)->preview);
}
static void action_zoom_out(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_zoom_out(SILKTEX_WINDOW(ud)->preview);
}
static void action_zoom_fit(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_zoom_fit_width(SILKTEX_WINDOW(ud)->preview);
}
static void action_zoom_fit_page(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_zoom_fit_page(SILKTEX_WINDOW(ud)->preview);
}
static void action_zoom_reset(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_set_zoom(SILKTEX_WINDOW(ud)->preview, 1.0);
}
static void action_prev_page(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_prev_page(SILKTEX_WINDOW(ud)->preview);
}
static void action_next_page(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_next_page(SILKTEX_WINDOW(ud)->preview);
}

static void change_preview_layout(GSimpleAction *action, GVariant *value, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    const char *mode = g_variant_get_string(value, NULL);
    SilktexPreviewLayout layout = SILKTEX_PREVIEW_LAYOUT_CONTINUOUS;
    if (g_strcmp0(mode, "single") == 0) layout = SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE;
    silktex_preview_set_layout(self->preview, layout);
    g_simple_action_set_state(action, value);
}

/* ------------------------------------------------------------ search */

static void action_find(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_searchbar_set_editor(self->searchbar, e);
    silktex_searchbar_open(self->searchbar, FALSE);
}
static void action_find_replace(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_searchbar_set_editor(self->searchbar, e);
    silktex_searchbar_open(self->searchbar, TRUE);
}
static void action_forward_sync(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (!e) return;
    const char *pdf = silktex_editor_get_pdffile(e);
    if (!pdf) return;
    if (!silktex_synctex_forward(e, self->preview, pdf))
        silktex_window_show_toast(self, _("SyncTeX: synctex not available or no .synctex.gz"));
}

/* ------------------------------------------------------------ misc */

static void action_toggle_preview(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    gboolean active = gtk_toggle_button_get_active(self->btn_preview);
    gtk_toggle_button_set_active(self->btn_preview, !active);
}

static void action_toggle_sidebar(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    gboolean active = gtk_toggle_button_get_active(self->btn_sidebar);
    gtk_toggle_button_set_active(self->btn_sidebar, !active);
}

static void action_preferences(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    /* AdwDialog releases its floating ref when closed, so build a fresh
     * preferences dialog every time. */
    SilktexPrefs *prefs = silktex_prefs_new();
    silktex_prefs_set_apply_callback(prefs, on_prefs_apply, self);
    silktex_prefs_set_snippets(prefs, self->snippets);
    silktex_prefs_present(prefs, GTK_WINDOW(self));
}

/*
 * Tri-state theme selector — mirrors gnome-text-editor's behaviour.
 * The config key [Interface] theme is one of "follow", "light", "dark"
 * and is applied through AdwStyleManager.  Called at startup and
 * whenever the user changes the selection from the menu.
 */
static void apply_theme_from_config(void)
{
    const char *mode = config_get_string("Interface", "theme");
    AdwStyleManager *sm = adw_style_manager_get_default();
    if (g_strcmp0(mode, "light") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
    else if (g_strcmp0(mode, "dark") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
}

static void update_theme_buttons(SilktexWindow *self, const char *mode)
{
    if (!mode || !*mode) mode = "follow";
    if (self->theme_follow)
        gtk_toggle_button_set_active(self->theme_follow, g_strcmp0(mode, "follow") == 0);
    if (self->theme_light)
        gtk_toggle_button_set_active(self->theme_light, g_strcmp0(mode, "light") == 0);
    if (self->theme_dark)
        gtk_toggle_button_set_active(self->theme_dark, g_strcmp0(mode, "dark") == 0);
}

static void change_theme(GSimpleAction *action, GVariant *value, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    const char *mode = g_variant_get_string(value, NULL);
    if (g_strcmp0(mode, "light") != 0 && g_strcmp0(mode, "dark") != 0) mode = "follow";
    config_set_string("Interface", "theme", mode);
    config_save();
    apply_theme_from_config();
    g_simple_action_set_state(action, value);
    update_theme_buttons(self, mode);
    apply_theme_to_all_editors(self);
}

static void on_system_dark_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (g_strcmp0(config_get_string("Interface", "theme"), "follow") == 0) {
        apply_theme_to_all_editors(self);
    }
}

static void action_fullscreen(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    self->is_fullscreen = !self->is_fullscreen;
    if (self->is_fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(self));
    else
        gtk_window_unfullscreen(GTK_WINDOW(self));
}

static void action_toggle_log(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (!self->log_toggle) return;
    gboolean active = gtk_toggle_button_get_active(self->log_toggle);
    gtk_toggle_button_set_active(self->log_toggle, !active);
}

static void on_log_toggle_active(GtkToggleButton *button, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (gtk_toggle_button_get_active(button)) {
        if (self->log_text_view) gtk_widget_grab_focus(self->log_text_view);
    } else {
        focus_active_editor(self);
    }
}

static void action_refresh_structure(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (self->structure) silktex_structure_refresh(self->structure);
}

/* ------------------------------------------------------------ compile aux */

static void action_run_bibtex(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (!e) return;
    gboolean ok = silktex_compiler_run_bibtex(self->compiler, e);
    silktex_window_show_toast(self, ok ? _("BibTeX finished") : _("BibTeX failed"));
}

static void action_run_makeindex(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (!e) return;
    gboolean ok = silktex_compiler_run_makeindex(self->compiler, e);
    silktex_window_show_toast(self, ok ? _("Makeindex finished") : _("Makeindex failed"));
}

static void action_cleanup(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (!e) return;

    const char *exts[] = {"aux", "log", "out", "toc", "bbl",        "blg", "idx",
                          "ilg", "ind", "lof", "lot", "synctex.gz", NULL};
    int removed = 0;

    /* Clean the cache-dir job files (new layout). */
    const char *work = silktex_editor_get_workfile(e);
    if (work) {
        g_autofree char *cache_dir = g_path_get_dirname(work);
        g_autofree char *cache_base = g_path_get_basename(work);
        char *d = strrchr(cache_base, '.');
        if (d) *d = '\0';
        for (int i = 0; exts[i]; i++) {
            g_autofree char *f = g_strdup_printf("%s/%s.%s", cache_dir, cache_base, exts[i]);
            if (g_file_test(f, G_FILE_TEST_EXISTS) && g_remove(f) == 0) removed++;
        }
    }

    /* Also mop up any legacy or user-run artefacts next to the source. */
    const char *fname = silktex_editor_get_filename(e);
    if (fname && *fname) {
        g_autofree char *src_dir = g_path_get_dirname(fname);
        g_autofree char *src_base = g_path_get_basename(fname);
        char *d = strrchr(src_base, '.');
        if (d) *d = '\0';
        for (int i = 0; exts[i]; i++) {
            g_autofree char *f = g_strdup_printf("%s/%s.%s", src_dir, src_base, exts[i]);
            if (g_file_test(f, G_FILE_TEST_EXISTS) && g_remove(f) == 0) removed++;
        }
    }

    g_autofree char *msg = g_strdup_printf(_("Removed %d build file(s)"), removed);
    silktex_window_show_toast(self, msg);
}

static void action_stats(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (!e) return;
    g_autofree char *text = silktex_editor_get_text(e);
    if (!text) return;

    int chars = 0, words = 0, lines = 0, math = 0;
    gboolean in_word = FALSE;
    gboolean in_math = FALSE;
    for (const char *p_ = text; *p_; p_++) {
        chars++;
        if (*p_ == '\n') lines++;
        if (*p_ == '$') {
            if (!in_math) math++;
            in_math = !in_math;
        }
        if (g_ascii_isspace(*p_)) {
            in_word = FALSE;
        } else if (!in_word) {
            words++;
            in_word = TRUE;
        }
    }

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Document Statistics"), NULL));
    g_autofree char *body = g_strdup_printf(
        _("Words: %d\nLines: %d\nCharacters: %d\nInline math: %d"), words, lines, chars, math);
    adw_alert_dialog_set_body(dlg, body);
    adw_alert_dialog_add_response(dlg, "ok", _("OK"));
    adw_alert_dialog_set_default_response(dlg, "ok");
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(self));
}

static void action_open_pdf_external(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (!e) return;
    const char *pdf = silktex_editor_get_pdffile(e);
    if (!pdf || !g_file_test(pdf, G_FILE_TEST_EXISTS)) {
        silktex_window_show_toast(self, _("No PDF available — compile first"));
        return;
    }
    g_autofree char *uri = g_filename_to_uri(pdf, NULL, NULL);
    if (uri) g_app_info_launch_default_for_uri(uri, NULL, NULL);
}

/* ------------------------------------------------------------ git */

typedef enum {
    GIT_OP_STAGE,
    GIT_OP_UNSTAGE,
    GIT_OP_COMMIT,
    GIT_OP_PULL,
    GIT_OP_PUSH,
} GitOperation;

typedef struct {
    GWeakRef win_ref;
    GitOperation op;
    char *repo_root;
    char *path;
    char *message;
} GitOperationData;

typedef struct {
    SilktexWindow *self;
    char *path;
    gboolean stage;
} GitFileActionData;

typedef struct {
    GWeakRef win_ref;
    char *path;
} GitStatusTaskData;

static void git_status_task_data_free(gpointer data)
{
    GitStatusTaskData *d = data;
    if (d == NULL) return;
    g_weak_ref_clear(&d->win_ref);
    g_free(d->path);
    g_free(d);
}

static void git_operation_data_free(gpointer data)
{
    GitOperationData *op = data;
    if (op == NULL) return;

    g_weak_ref_clear(&op->win_ref);
    g_free(op->repo_root);
    g_free(op->path);
    g_free(op->message);
    g_free(op);
}

static void git_file_action_data_free(gpointer data)
{
    GitFileActionData *file_action = data;
    if (file_action == NULL) return;

    g_clear_object(&file_action->self);
    g_free(file_action->path);
    g_free(file_action);
}

static void git_file_action_data_destroy(gpointer data, GClosure *closure)
{
    (void)closure;
    git_file_action_data_free(data);
}

static void set_action_enabled(SilktexWindow *self, const char *name, gboolean enabled)
{
    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), name);
    if (G_IS_SIMPLE_ACTION(action)) {
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
    }
}

static void update_git_actions(SilktexWindow *self)
{
    gboolean has_git = silktex_git_is_available();

    set_action_enabled(self, "git-status", has_git);
    set_action_enabled(self, "git-commit", has_git);
    set_action_enabled(self, "git-pull", has_git);
    set_action_enabled(self, "git-push", has_git);
}

static const char *status_label_for_file(const SilktexGitFile *file)
{
    if (file->index_status == '?' && file->worktree_status == '?') return _("Untracked");
    if (file->index_status != ' ' && file->worktree_status != ' ') return _("Staged and modified");
    if (file->index_status != ' ') return _("Staged");
    if (file->worktree_status != ' ') return _("Modified");
    return _("Clean");
}

static gboolean file_has_staged_change(const SilktexGitFile *file)
{
    return file->index_status != ' ' && file->index_status != '?';
}

static gboolean file_has_unstaged_change(const SilktexGitFile *file)
{
    return file->worktree_status != ' ' || file->index_status == '?';
}

static void clear_git_list(SilktexWindow *self)
{
    if (self->git_list == NULL) return;

    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(self->git_list));
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(self->git_list, child);
        child = next;
    }
}

static void git_operation_thread(GTask *task, gpointer source_object, gpointer task_data,
                                 GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;

    GitOperationData *op = task_data;
    GError *error = NULL;
    char *output = NULL;
    gboolean ok = FALSE;

    switch (op->op) {
    case GIT_OP_STAGE:
        ok = silktex_git_stage_file(op->repo_root, op->path, &error);
        break;
    case GIT_OP_UNSTAGE:
        ok = silktex_git_unstage_file(op->repo_root, op->path, &error);
        break;
    case GIT_OP_COMMIT:
        ok = silktex_git_commit(op->repo_root, op->message, &output, &error);
        break;
    case GIT_OP_PULL:
        ok = silktex_git_pull(op->repo_root, &output, &error);
        break;
    case GIT_OP_PUSH:
        ok = silktex_git_push(op->repo_root, &output, &error);
        break;
    }

    if (!ok) {
        g_task_return_error(task, error);
        return;
    }

    g_task_return_pointer(task, output ? output : g_strdup(""), g_free);
}

static const char *success_message_for_operation(GitOperation op)
{
    switch (op) {
    case GIT_OP_STAGE:
        return _("File staged");
    case GIT_OP_UNSTAGE:
        return _("File unstaged");
    case GIT_OP_COMMIT:
        return _("Commit created");
    case GIT_OP_PULL:
        return _("Pull completed");
    case GIT_OP_PUSH:
        return _("Push completed");
    }
    return _("Git operation completed");
}

static void on_git_operation_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)source;
    (void)user_data;
    GTask *task = G_TASK(result);
    GitOperationData *op = g_task_get_task_data(task);
    if (op == NULL) return;

    SilktexWindow *self = g_weak_ref_get(&op->win_ref);
    if (self == NULL) return;

    GError *error = NULL;
    g_autofree char *output = g_task_propagate_pointer(task, &error);

    if (error != NULL) {
        silktex_window_show_toast(self, error->message);
        g_clear_error(&error);
    } else {
        const char *message = success_message_for_operation(op->op);
        silktex_window_show_toast(self, message);
        if (op->op == GIT_OP_COMMIT && self->git_commit_message != NULL) {
            gtk_editable_set_text(self->git_commit_message, "");
        }
    }

    refresh_git_state(self);
    update_git_dialog(self);
    g_object_unref(self);
}

static void run_git_operation(SilktexWindow *self, GitOperation op, const char *path,
                              const char *message)
{
    if (self->git_status == NULL || self->git_status->repo_root == NULL) {
        silktex_window_show_toast(self, _("No Git repository for the active document"));
        return;
    }

    GitOperationData *data = g_new0(GitOperationData, 1);
    g_weak_ref_init(&data->win_ref, G_OBJECT(self));
    data->op = op;
    data->repo_root = g_strdup(self->git_status->repo_root);
    data->path = g_strdup(path);
    data->message = g_strdup(message);

    GTask *task = g_task_new(NULL, NULL, on_git_operation_finished, NULL);
    g_task_set_task_data(task, data, git_operation_data_free);
    g_task_run_in_thread(task, git_operation_thread);
    g_object_unref(task);
}

static void on_git_stage_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GitFileActionData *data = user_data;
    run_git_operation(data->self, data->stage ? GIT_OP_STAGE : GIT_OP_UNSTAGE, data->path, NULL);
}

static void add_git_status_row(SilktexWindow *self, const SilktexGitFile *file)
{
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), file->path);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), status_label_for_file(file));

    if (file_has_unstaged_change(file)) {
        GtkWidget *button = gtk_button_new_with_label(_("Stage"));
        gtk_widget_add_css_class(button, "flat");

        GitFileActionData *data = g_new0(GitFileActionData, 1);
        data->self = g_object_ref(self);
        data->path = g_strdup(file->path);
        data->stage = TRUE;
        g_signal_connect_data(button, "clicked", G_CALLBACK(on_git_stage_clicked), data,
                              git_file_action_data_destroy, 0);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), button);
    }

    if (file_has_staged_change(file)) {
        GtkWidget *button = gtk_button_new_with_label(_("Unstage"));
        gtk_widget_add_css_class(button, "flat");

        GitFileActionData *data = g_new0(GitFileActionData, 1);
        data->self = g_object_ref(self);
        data->path = g_strdup(file->path);
        data->stage = FALSE;
        g_signal_connect_data(button, "clicked", G_CALLBACK(on_git_stage_clicked), data,
                              git_file_action_data_destroy, 0);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), button);
    }

    gtk_list_box_append(self->git_list, row);
}

static void update_git_dialog(SilktexWindow *self)
{
    if (self->git_dialog == NULL) return;

    clear_git_list(self);

    if (self->git_status == NULL) {
        gtk_label_set_label(self->git_branch_label, _("No Git repository"));
        gtk_label_set_label(self->git_repo_label,
                            self->git_status_message ? self->git_status_message
                                                     : _("Save a file inside a Git repository."));

        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("No changes to show"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), _("Open or save a document in a Git repository."));
        gtk_list_box_append(self->git_list, row);
        return;
    }

    g_autofree char *branch = g_strdup_printf(_("Branch: %s"), self->git_status->branch);
    gtk_label_set_label(self->git_branch_label, branch);
    gtk_label_set_label(self->git_repo_label, self->git_status->repo_root);

    if (self->git_status->files->len == 0) {
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Working Tree Clean"));
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), _("There are no staged or unstaged changes."));
        gtk_list_box_append(self->git_list, row);
        return;
    }

    for (guint i = 0; i < self->git_status->files->len; i++) {
        SilktexGitFile *file = g_ptr_array_index(self->git_status->files, i);
        add_git_status_row(self, file);
    }
}

static void git_status_thread(GTask *task, gpointer source_object, gpointer task_data,
                              GCancellable *cancellable)
{
    (void)source_object;
    (void)task_data;
    (void)cancellable;

    GitStatusTaskData *d = g_task_get_task_data(task);
    if (d == NULL || d->path == NULL) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Git status: missing path");
        return;
    }

    GError *error = NULL;
    SilktexGitStatus *status = silktex_git_status_load(d->path, &error);
    if (status == NULL) {
        g_task_return_error(task, error);
        return;
    }

    g_task_return_pointer(task, status, (GDestroyNotify)silktex_git_status_free);
}

static void on_git_status_loaded(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)source;
    (void)user_data;
    GTask *task = G_TASK(result);
    GitStatusTaskData *td = g_task_get_task_data(task);
    const char *requested_path = (td && td->path) ? td->path : NULL;

    GError *error = NULL;
    SilktexGitStatus *status = g_task_propagate_pointer(task, &error);

    SilktexWindow *self = NULL;
    if (td != NULL) self = g_weak_ref_get(&td->win_ref);

    if (self == NULL) {
        if (status != NULL) silktex_git_status_free(status);
        g_clear_error(&error);
        return;
    }

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    const char *current_path = editor ? silktex_editor_get_filename(editor) : NULL;
    if (g_strcmp0(requested_path, current_path) != 0) {
        if (status != NULL) silktex_git_status_free(status);
        g_clear_error(&error);
        g_object_unref(self);
        return;
    }

    g_clear_pointer(&self->git_status, silktex_git_status_free);
    g_clear_pointer(&self->git_status_message, g_free);

    if (error != NULL) {
        self->git_status_message = g_strdup(error->message);
        g_clear_error(&error);
    } else {
        self->git_status = status;
    }

    update_git_actions(self);
    update_git_dialog(self);
    g_object_unref(self);
}

static void refresh_git_state(SilktexWindow *self)
{
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    const char *filename = editor ? silktex_editor_get_filename(editor) : NULL;

    if (!silktex_git_is_available() || filename == NULL || *filename == '\0') {
        g_clear_pointer(&self->git_status, silktex_git_status_free);
        g_clear_pointer(&self->git_status_message, g_free);
        self->git_status_message =
            g_strdup(!silktex_git_is_available() ? _("git is not installed")
                                                 : _("Save the active document before using Git"));
        update_git_actions(self);
        update_git_dialog(self);
        return;
    }

    GitStatusTaskData *d = g_new0(GitStatusTaskData, 1);
    g_weak_ref_init(&d->win_ref, G_OBJECT(self));
    d->path = g_strdup(filename);

    GTask *task = g_task_new(NULL, NULL, on_git_status_loaded, NULL);
    g_task_set_task_data(task, d, git_status_task_data_free);
    g_task_run_in_thread(task, git_status_thread);
    g_object_unref(task);
}

static void on_git_dialog_closed(AdwDialog *dialog, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (self->git_dialog == dialog) {
        self->git_dialog = NULL;
        self->git_branch_label = NULL;
        self->git_repo_label = NULL;
        self->git_list = NULL;
        self->git_commit_message = NULL;
    }
}

static void on_git_dialog_refresh_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    refresh_git_state(SILKTEX_WINDOW(user_data));
}

static void on_git_dialog_commit_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    const char *message =
        self->git_commit_message ? gtk_editable_get_text(self->git_commit_message) : "";
    g_autofree char *trimmed = g_strdup(message ? message : "");
    if (*g_strstrip(trimmed) == '\0') {
        silktex_window_show_toast(self, _("Enter a commit message"));
        return;
    }

    run_git_operation(self, GIT_OP_COMMIT, NULL, trimmed);
}

static GtkWidget *build_git_dialog_content(SilktexWindow *self)
{
    GtkWidget *toolbarview = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarview), header);

    GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh, _("Refresh Git Status"));
    gtk_widget_add_css_class(refresh, "flat");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_git_dialog_refresh_clicked), self);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), refresh);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    self->git_branch_label = GTK_LABEL(gtk_label_new(_("No Git repository")));
    gtk_label_set_xalign(self->git_branch_label, 0.0);
    gtk_widget_add_css_class(GTK_WIDGET(self->git_branch_label), "heading");
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(self->git_branch_label));

    self->git_repo_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(self->git_repo_label, 0.0);
    gtk_label_set_ellipsize(self->git_repo_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(GTK_WIDGET(self->git_repo_label), "dim-label");
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(self->git_repo_label));

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 260);
    self->git_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(self->git_list, GTK_SELECTION_NONE);
    gtk_widget_add_css_class(GTK_WIDGET(self->git_list), "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(self->git_list));
    gtk_box_append(GTK_BOX(box), scroll);

    GtkWidget *commit_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    self->git_commit_message = GTK_EDITABLE(gtk_entry_new());
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->git_commit_message), _("Commit message"));
    gtk_widget_set_hexpand(GTK_WIDGET(self->git_commit_message), TRUE);
    gtk_box_append(GTK_BOX(commit_box), GTK_WIDGET(self->git_commit_message));

    GtkWidget *commit = gtk_button_new_with_label(_("Commit"));
    gtk_widget_add_css_class(commit, "suggested-action");
    g_signal_connect(commit, "clicked", G_CALLBACK(on_git_dialog_commit_clicked), self);
    gtk_box_append(GTK_BOX(commit_box), commit);
    gtk_box_append(GTK_BOX(box), commit_box);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), box);
    return toolbarview;
}

static void show_git_dialog(SilktexWindow *self)
{
    if (self->git_dialog != NULL) {
        adw_dialog_present(self->git_dialog, GTK_WIDGET(self));
        update_git_dialog(self);
        return;
    }

    AdwDialog *dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, _("Git Status"));
    adw_dialog_set_content_width(dialog, 680);
    adw_dialog_set_content_height(dialog, 520);
    adw_dialog_set_child(dialog, build_git_dialog_content(self));
    g_signal_connect(dialog, "closed", G_CALLBACK(on_git_dialog_closed), self);

    self->git_dialog = dialog;
    update_git_dialog(self);
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void action_git_status(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    show_git_dialog(self);
    refresh_git_state(self);
}

static gboolean idle_focus_git_commit_entry(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (self->git_commit_message != NULL)
        gtk_widget_grab_focus(GTK_WIDGET(self->git_commit_message));
    return G_SOURCE_REMOVE;
}

static void action_git_commit(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    show_git_dialog(self);
    g_idle_add(idle_focus_git_commit_entry, self);
}

static void action_git_pull(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    run_git_operation(SILKTEX_WINDOW(ud), GIT_OP_PULL, NULL, NULL);
}

static void action_git_push(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    run_git_operation(SILKTEX_WINDOW(ud), GIT_OP_PUSH, NULL, NULL);
}

/* ------------------------------------------------------------ recent files */

typedef struct {
    SilktexWindow *self;
    AdwDialog *dialog;
    char *uri;
} RecentItem;

static void recent_item_free(gpointer data)
{
    RecentItem *r = data;
    g_free(r->uri);
    g_free(r);
}

static void on_recent_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer ud)
{
    RecentItem *r = g_object_get_data(G_OBJECT(row), "recent-item");
    if (!r || !r->uri) return;
    GFile *file = g_file_new_for_uri(r->uri);
    silktex_window_open_file(r->self, file);
    g_object_unref(file);
    if (r->dialog) adw_dialog_close(r->dialog);
}

static void on_recent_clear(GtkButton *btn, gpointer ud)
{
    AdwDialog *dlg = ADW_DIALOG(ud);
    GtkRecentManager *mgr = gtk_recent_manager_get_default();
    gtk_recent_manager_purge_items(mgr, NULL);
    adw_dialog_close(dlg);
}

static void action_show_recent(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);

    AdwDialog *dlg = adw_dialog_new();
    adw_dialog_set_title(dlg, _("Open Recent"));
    adw_dialog_set_content_width(dlg, 520);
    adw_dialog_set_content_height(dlg, 440);

    GtkWidget *toolbarview = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarview), hb);

    GtkWidget *clear = gtk_button_new_with_label(_("Clear"));
    gtk_widget_add_css_class(clear, "flat");
    g_signal_connect(clear, "clicked", G_CALLBACK(on_recent_clear), dlg);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), clear);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "navigation-sidebar");
    g_signal_connect(list, "row-activated", G_CALLBACK(on_recent_row_activated), self);

    GtkRecentManager *mgr = gtk_recent_manager_get_default();
    GList *items = gtk_recent_manager_get_items(mgr);
    int added = 0;
    for (GList *l = items; l != NULL; l = l->next) {
        GtkRecentInfo *info = l->data;
        const char *uri = gtk_recent_info_get_uri(info);
        if (!uri) continue;
        const char *mime = gtk_recent_info_get_mime_type(info);
        const char *name = gtk_recent_info_get_display_name(info);
        if (!g_str_has_prefix(uri, "file://")) continue;
        gboolean is_tex = FALSE;
        if (g_str_has_suffix(uri, ".tex") || g_str_has_suffix(uri, ".ltx") ||
            g_str_has_suffix(uri, ".sty") || g_str_has_suffix(uri, ".cls"))
            is_tex = TRUE;
        if (mime &&
            (g_str_has_prefix(mime, "text/x-tex") || g_str_has_prefix(mime, "text/x-latex")))
            is_tex = TRUE;
        if (!is_tex) continue;

        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                      name ? name : gtk_recent_info_get_short_name(info));
        g_autofree char *path = g_filename_from_uri(uri, NULL, NULL);
        if (path) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), path);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);

        RecentItem *ri = g_new0(RecentItem, 1);
        ri->self = self;
        ri->dialog = dlg;
        ri->uri = g_strdup(uri);
        g_object_set_data_full(G_OBJECT(row), "recent-item", ri, recent_item_free);

        gtk_list_box_append(GTK_LIST_BOX(list), row);
        added++;
        if (added >= 40) break;
    }
    g_list_free_full(items, (GDestroyNotify)gtk_recent_info_unref);

    if (added == 0) {
        GtkWidget *status = adw_status_page_new();
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "document-open-recent-symbolic");
        adw_status_page_set_title(ADW_STATUS_PAGE(status), _("No Recent Files"));
        adw_status_page_set_description(ADW_STATUS_PAGE(status),
                                        _("Recently opened LaTeX files will appear here."));
        adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), status);
    } else {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
        adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), scroll);
    }

    adw_dialog_set_child(dlg, toolbarview);
    adw_dialog_present(dlg, GTK_WIDGET(self));
}

/*
 * Build a proper GtkShortcutsWindow rather than dumping shortcut text
 * into an alert dialog.  Sections/groups match the GNOME HIG so the
 * result looks consistent with other Adwaita apps.
 */
static GtkWidget *make_shortcut(const char *accel, const char *title)
{
    return g_object_new(GTK_TYPE_SHORTCUTS_SHORTCUT, "accelerator", accel, "title", title, NULL);
}

static GtkWidget *make_group(const char *title)
{
    return g_object_new(GTK_TYPE_SHORTCUTS_GROUP, "title", title, NULL);
}

static void action_shortcuts(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);

    /* GtkShortcutsWindow and related APIs are deprecated as of 4.18 (removed in GTK 5) with
     * no in-tree replacement; GtkBuilder from .ui is equally deprecated. Silence until
     * we can ship a custom shortcuts dialog or migrate to a future API. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *win = g_object_new(GTK_TYPE_SHORTCUTS_WINDOW, "modal", TRUE, "transient-for", self,
                                  "destroy-with-parent", TRUE, NULL);

    GtkWidget *section = g_object_new(GTK_TYPE_SHORTCUTS_SECTION, "section-name", "main",
                                      "visible", TRUE, NULL);

    GtkWidget *g_files = make_group(_("Files"));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_files),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>n", _("New tab"))));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_files),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>o", _("Open"))));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_files),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>s", _("Save"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_files),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary><Shift>s", _("Save As"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_files),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>q", _("Quit"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_files));

    GtkWidget *g_edit = make_group(_("Editing"));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_edit),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>z", _("Undo"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary><Shift>z", _("Redo"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>b", _("Bold"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>i", _("Italic"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>u", _("Underline"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>f", _("Find"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>h", _("Find & Replace"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_edit));

    GtkWidget *g_doc = make_group(_("Document"));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_doc),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>Return", _("Compile"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_doc),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary><Shift>f", _("Forward SyncTeX"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_doc));

    GtkWidget *g_view = make_group(_("View"));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>plus", _("Zoom in"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>minus", _("Zoom out"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>0", _("Fit width"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F8", _("Toggle outline"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F9", _("Toggle preview"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F10", _("Main menu"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F11", _("Fullscreen"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>question", _("Keyboard shortcuts"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>comma", _("Preferences"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_view));

    gtk_shortcuts_window_add_section(GTK_SHORTCUTS_WINDOW(win), GTK_SHORTCUTS_SECTION(section));
    gtk_window_present(GTK_WINDOW(win));
    G_GNUC_END_IGNORE_DEPRECATIONS
}

typedef struct {
    SilktexWindow *self;
    char *action;
} DeferredAction;

static gboolean activate_deferred_action(gpointer user_data)
{
    DeferredAction *data = user_data;
    gtk_widget_activate_action(GTK_WIDGET(data->self), data->action, NULL);
    g_object_unref(data->self);
    g_free(data->action);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void on_primary_popover_action(GtkButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    const char *action = g_object_get_data(G_OBJECT(button), "silktex-action");

    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }

    if (action != NULL) {
        DeferredAction *data = g_new0(DeferredAction, 1);
        data->self = g_object_ref(self);
        data->action = g_strdup(action);
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, activate_deferred_action, data, NULL);
    }
}

static GtkWidget *make_primary_popover_button(const char *label, const char *icon_name,
                                              const char *action, SilktexWindow *self)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_hexpand(button, TRUE);
    g_object_set_data_full(G_OBJECT(button), "silktex-action", g_strdup(action), g_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *lbl = gtk_label_new_with_mnemonic(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(box), lbl);

    gtk_button_set_child(GTK_BUTTON(button), box);
    g_signal_connect(button, "clicked", G_CALLBACK(on_primary_popover_action), self);
    return button;
}

static void install_theme_swatch_css(void)
{
    static gboolean installed = FALSE;
    if (installed) return;
    installed = TRUE;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        "button.theme-swatch-button,"
        "button.theme-swatch-button:hover,"
        "button.theme-swatch-button:checked,"
        "button.theme-swatch-button:checked:hover {"
        "  padding: 0;"
        "  min-width: 0;"
        "  min-height: 0;"
        "  background: transparent;"
        "  border-color: transparent;"
        "  box-shadow: none;"
        "  outline-color: transparent;"
        "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* Single-pixel paned grip: no blur to the side (that shifted the vertical line
 * vs. the footer) and no double stroke (background + outline). */
static void install_silktex_chrome_css(void)
{
    static gboolean installed = FALSE;
    if (installed) return;
    installed = TRUE;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        "paned.silktex-editor-paned.horizontal > separator {"
        "  min-width: 1px;"
        "  min-height: 1px;"
        "  background: @borders;"
        "  box-shadow: none;"
        "}"
        "paned.silktex-editor-paned.vertical > separator {"
        "  min-width: 1px;"
        "  min-height: 1px;"
        "  background: @borders;"
        "  box-shadow: none;"
        "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_theme_selector_clicked(GtkToggleButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (!gtk_toggle_button_get_active(button)) return;

    const char *mode = g_object_get_data(G_OBJECT(button), "silktex-theme");
    if (mode != NULL) {
        g_action_group_activate_action(G_ACTION_GROUP(self), "set-theme", g_variant_new_string(mode));
    }
}

static void draw_theme_swatch(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                              gpointer user_data)
{
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(user_data);
    const char *mode = g_object_get_data(G_OBJECT(button), "silktex-theme");
    gboolean active = gtk_toggle_button_get_active(button);

    double size = MIN(width, height) - 4.0;
    double x = (width - size) / 2.0;
    double y = (height - size) / 2.0;
    double r = size / 2.0;
    double cx = x + r;
    double cy = y + r;

    cairo_save(cr);
    cairo_arc(cr, cx, cy, r - 1.0, 0, 2 * G_PI);
    cairo_clip(cr);

    if (g_strcmp0(mode, "follow") == 0) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_move_to(cr, x + size, y);
        cairo_line_to(cr, x + size, y + size);
        cairo_line_to(cr, x, y + size);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.09);
        cairo_fill(cr);
    } else if (g_strcmp0(mode, "dark") == 0) {
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.09);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    cairo_restore(cr);

    cairo_arc(cr, cx, cy, r - 1.0, 0, 2 * G_PI);
    if (active) {
        cairo_set_source_rgb(cr, 0.21, 0.52, 0.89);
        cairo_set_line_width(cr, 2.0);
    } else {
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.35);
        cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    if (active) {
        double br = 8.5;
        double bx = x + size - br - 1.0;
        double by = y + size - br - 1.0;

        cairo_arc(cr, bx, by, br, 0, 2 * G_PI);
        cairo_set_source_rgb(cr, 0.21, 0.52, 0.89);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, bx - 3.5, by);
        cairo_line_to(cr, bx - 0.8, by + 3.0);
        cairo_line_to(cr, bx + 4.5, by - 4.0);
        cairo_stroke(cr);
    }
}

static void on_theme_swatch_active_changed(GtkToggleButton *button, GParamSpec *pspec,
                                           gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    GtkWidget *child = gtk_button_get_child(GTK_BUTTON(button));
    if (GTK_IS_DRAWING_AREA(child) && gtk_widget_get_width(child) > 0 &&
        gtk_widget_get_height(child) > 0)
        gtk_widget_queue_draw(child);
}

static void on_primary_popover_show(GtkWidget *popover, gpointer user_data)
{
    (void)popover;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    update_theme_buttons(self, config_get_string("Interface", "theme"));
}

static GtkWidget *make_theme_toggle(const char *mode, const char *tooltip, SilktexWindow *self)
{
    GtkWidget *button = gtk_toggle_button_new();
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "theme-swatch-button");

    g_object_set_data_full(G_OBJECT(button), "silktex-theme", g_strdup(mode), g_free);

    GtkWidget *swatch = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_theme_swatch, button, NULL);
    gtk_widget_set_size_request(swatch, 52, 52);
    gtk_widget_set_margin_start(swatch, 4);
    gtk_widget_set_margin_end(swatch, 4);
    gtk_widget_set_margin_top(swatch, 2);
    gtk_widget_set_margin_bottom(swatch, 2);
    gtk_button_set_child(GTK_BUTTON(button), swatch);

    g_signal_connect(button, "clicked", G_CALLBACK(on_theme_selector_clicked), self);
    g_signal_connect(button, "notify::active", G_CALLBACK(on_theme_swatch_active_changed), NULL);
    return button;
}

static void install_primary_popover(SilktexWindow *self)
{
    install_theme_swatch_css();

    GtkWidget *popover = gtk_popover_new();
    /* Click outside the menu to dismiss (same behavior as a standard menu). */
    gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    GtkWidget *theme = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(theme, GTK_ALIGN_CENTER);

    GtkWidget *follow = make_theme_toggle("follow", _("Follow System Theme"), self);
    GtkWidget *light = make_theme_toggle("light", _("Light Theme"), self);
    GtkWidget *dark = make_theme_toggle("dark", _("Dark Theme"), self);
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(light), GTK_TOGGLE_BUTTON(follow));
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(dark), GTK_TOGGLE_BUTTON(follow));

    self->theme_follow = GTK_TOGGLE_BUTTON(follow);
    self->theme_light = GTK_TOGGLE_BUTTON(light);
    self->theme_dark = GTK_TOGGLE_BUTTON(dark);

    gtk_box_append(GTK_BOX(theme), follow);
    gtk_box_append(GTK_BOX(theme), light);
    gtk_box_append(GTK_BOX(theme), dark);
    gtk_box_append(GTK_BOX(box), theme);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_New"), "document-new-symbolic", "win.new", self));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(_("_Open…"),
                                                             "document-open-symbolic", "win.open",
                                                             self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("Open _Recent…"), "document-open-recent-symbolic",
                                               "win.show-recent", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Save"), "document-save-symbolic", "win.save", self));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(_("Save _As…"),
                                                             "document-save-as-symbolic",
                                                             "win.save-as", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Export to PDF…"), "document-save-as-symbolic",
                                               "win.export-pdf", self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(
                         _("_Git Status…"), "vcs-git-symbolic", "win.git-status", self));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(
                         _("Git _Commit…"), "emblem-ok-symbolic", "win.git-commit", self));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(
                         _("Git _Pull"), "network-receive-symbolic", "win.git-pull", self));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(
                         _("Git P_ush"), "network-transmit-symbolic", "win.git-push", self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Fullscreen"), "view-fullscreen-symbolic",
                                               "win.fullscreen", self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Preferences"), "emblem-system-symbolic",
                                               "win.preferences", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("Keyboard _Shortcuts"), "preferences-desktop-keyboard-symbolic",
                                               "win.shortcuts", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_About SilkTex"), "help-about-symbolic",
                                               "app.about", self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Quit"), "application-exit-symbolic", "app.quit", self));

    gtk_menu_button_set_menu_model(self->btn_menu, NULL);
    gtk_menu_button_set_popover(self->btn_menu, popover);
    g_signal_connect(popover, "show", G_CALLBACK(on_primary_popover_show), self);
}

/* Open the primary ("hamburger") menu.  F10 activates this from anywhere. */
static void action_open_menu(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (self->btn_menu) gtk_menu_button_popup(self->btn_menu);
}

/* ------------------------------------------------------------ preferences */

static void on_prefs_apply(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    guint n = adw_tab_view_get_n_pages(self->tab_view);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(self->tab_view, i);
        SilktexEditor *editor = get_editor_for_page(page);
        if (editor) {
            silktex_editor_apply_settings(editor);
            apply_theme_to_editor(editor);
        }
    }
    config_save();
    self->auto_compile = config_get_boolean("Compile", "auto_compile");
    silktex_compiler_apply_config(self->compiler);
    restart_autosave_timer(self);

    /* Snippet shortcuts may have changed; push the new modifier pair
     * through to the running engine so shortcuts update immediately. */
    if (self->snippets) {
        silktex_snippets_set_modifiers(self->snippets, config_get_string("Snippets", "modifier1"),
                                       config_get_string("Snippets", "modifier2"));
    }
}

/* ------------------------------------------------------------ preview toggle */

/* Clamp the GtkPaned start-child width (left / editor) to allowed range. */
static int clamp_editor_pane_start(int w, int pos)
{
    if (w < 1) return pos;
    int max_start = w - PREVIEW_PANE_MIN_WIDTH;
    if (max_start < EDITOR_MIN_WIDTH) max_start = EDITOR_MIN_WIDTH;
    if (pos < EDITOR_MIN_WIDTH) return EDITOR_MIN_WIDTH;
    if (pos > max_start) return max_start;
    return pos;
}

static void apply_editor_paned_half_split(SilktexWindow *self)
{
    if (self->editor_paned == NULL) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
    if (w < 1) return;

    int half = w / 2;
    int max_start = w - PREVIEW_PANE_MIN_WIDTH;
    if (max_start < EDITOR_MIN_WIDTH) max_start = EDITOR_MIN_WIDTH;
    if (half > max_start) half = max_start;
    if (half < EDITOR_MIN_WIDTH) half = EDITOR_MIN_WIDTH;
    if (half > max_start) half = (EDITOR_MIN_WIDTH + max_start) / 2;

    self->preview_pane_silence = TRUE;
    gtk_paned_set_position(self->editor_paned, half);
    self->preview_pane_silence = FALSE;
    self->preview_pane_pos = gtk_paned_get_position(self->editor_paned);
    self->preview_pane_restorable = TRUE;
    self->preview_split_seeded = TRUE;
}

/* Re-apply 50% or the last stored split; call only when the preview pane is visible. */
static void apply_editor_pane_restore(SilktexWindow *self)
{
    if (self->editor_paned == NULL || self->preview_toolbar_view == NULL) return;
    if (!gtk_widget_get_visible(GTK_WIDGET(self->preview_toolbar_view))) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
    if (w < 1) return;

    if (!self->preview_pane_restorable) {
        apply_editor_paned_half_split(self);
        return;
    }

    int pos = clamp_editor_pane_start(w, self->preview_pane_pos);
    self->preview_pane_silence = TRUE;
    gtk_paned_set_position(self->editor_paned, pos);
    self->preview_pane_silence = FALSE;
    self->preview_pane_pos = gtk_paned_get_position(self->editor_paned);
    self->preview_split_seeded = TRUE;
}

static gboolean idle_restore_editor_pane(gpointer user_data)
{
    apply_editor_pane_restore(SILKTEX_WINDOW(user_data));
    return G_SOURCE_REMOVE;
}

static void on_preview_toggled(GtkToggleButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    gboolean visible = gtk_toggle_button_get_active(button);

    if (!visible && self->editor_paned && self->preview_toolbar_view &&
        gtk_widget_get_visible(GTK_WIDGET(self->preview_toolbar_view))) {
        int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
        if (w > 0) {
            self->preview_pane_pos = gtk_paned_get_position(self->editor_paned);
            self->preview_pane_restorable = TRUE;
        }
    }

    if (self->preview_toolbar_view)
        gtk_widget_set_visible(GTK_WIDGET(self->preview_toolbar_view), visible);
    gtk_button_set_icon_name(GTK_BUTTON(button),
                             visible ? "view-dual-symbolic" : "view-continuous-symbolic");

    if (visible) {
        int w = self->editor_paned ? gtk_widget_get_width(GTK_WIDGET(self->editor_paned)) : 0;
        if (w > 0)
            apply_editor_pane_restore(self);
        else
            g_idle_add(idle_restore_editor_pane, self);
    }
}

static void on_window_width_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    int width = gtk_widget_get_width(GTK_WIDGET(self));
    gboolean narrow = width > 0 && width < 1024;
    if (narrow == self->preview_narrow) return;

    self->preview_narrow = narrow;

    if (narrow && gtk_toggle_button_get_active(self->btn_preview)) {
        self->preview_auto_collapsed = TRUE;
        gtk_toggle_button_set_active(self->btn_preview, FALSE);
    } else if (!narrow && self->preview_auto_collapsed) {
        self->preview_auto_collapsed = FALSE;
        gtk_toggle_button_set_active(self->btn_preview, TRUE);
        g_idle_add(idle_restore_editor_pane, self);
    }
}

static void on_editor_paned_position_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    if (self->preview_pane_silence) return;
    if (!gtk_widget_get_visible(GTK_WIDGET(self->preview_toolbar_view))) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
    int position = gtk_paned_get_position(self->editor_paned);
    int clamped = clamp_editor_pane_start(w, position);
    if (clamped != position) {
        self->preview_pane_silence = TRUE;
        gtk_paned_set_position(self->editor_paned, clamped);
        self->preview_pane_silence = FALSE;
    }
    if (!self->preview_split_seeded) return;
    self->preview_pane_pos = gtk_paned_get_position(self->editor_paned);
    self->preview_pane_restorable = TRUE;
}

static void on_tab_changed(AdwTabView *view, GParamSpec *pspec, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    update_window_title(self);

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        silktex_searchbar_set_editor(self->searchbar, editor);
        silktex_structure_set_editor(self->structure, editor);
        if (self->auto_compile) restart_compile_timer(self);
    }
    refresh_git_state(self);
}

typedef struct {
    AdwTabView *view;
    AdwTabPage *page;
    SilktexWindow *win;
} ClosePageData;

static void on_close_dialog_response(AdwAlertDialog *dialog, const char *response,
                                     gpointer user_data)
{
    ClosePageData *d = user_data;
    SilktexEditor *editor = get_editor_for_page(d->page);

    if (g_strcmp0(response, "save") == 0 && editor) {
        const char *fname = silktex_editor_get_filename(editor);
        if (fname && *fname) {
            GFile *f = g_file_new_for_path(fname);
            silktex_editor_save_file(editor, f, NULL);
            g_object_unref(f);
        }
        adw_tab_view_close_page_finish(d->view, d->page, TRUE);
    } else if (g_strcmp0(response, "discard") == 0) {
        adw_tab_view_close_page_finish(d->view, d->page, TRUE);
    } else {
        adw_tab_view_close_page_finish(d->view, d->page, FALSE);
    }

    g_free(d);
}

static gboolean on_close_page(AdwTabView *view, AdwTabPage *page, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = get_editor_for_page(page);

    if (editor == NULL || !silktex_editor_get_modified(editor)) return GDK_EVENT_PROPAGATE;

    ClosePageData *d = g_new(ClosePageData, 1);
    d->view = view;
    d->page = page;
    d->win = self;

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Save Changes?"), _("This document has unsaved changes. Save before closing?")));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "discard", _("Discard"));
    adw_alert_dialog_add_response(dlg, "save", _("Save"));
    adw_alert_dialog_set_response_appearance(dlg, "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(dlg, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "save");
    adw_alert_dialog_set_close_response(dlg, "cancel");

    g_signal_connect(dlg, "response", G_CALLBACK(on_close_dialog_response), d);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(self));

    return GDK_EVENT_STOP;
}

/* ------------------------------------------------------------ actions[] */

static const GActionEntry win_actions[] = {
    {"new", action_new},
    {"open", action_open},
    {"save", action_save},
    {"save-as", action_save_as},
    {"export-pdf", action_export_pdf},
    {"close-tab", action_close_tab},
    {"undo", action_undo},
    {"redo", action_redo},
    {"compile", action_compile},
    {"bold", action_bold},
    {"italic", action_italic},
    {"underline", action_underline},
    {"align-left", action_align_left},
    {"align-center", action_align_center},
    {"align-right", action_align_right},
    {"insert-section", action_insert_section},
    {"insert-subsection", action_insert_subsection},
    {"insert-subsubsection", action_insert_subsubsection},
    {"insert-chapter", action_insert_chapter},
    {"insert-paragraph", action_insert_paragraph},
    {"insert-itemize", action_insert_itemize},
    {"insert-enumerate", action_insert_enumerate},
    {"insert-description", action_insert_description},
    {"insert-equation", action_insert_equation},
    {"insert-quote", action_insert_quote},
    {"insert-image", action_insert_image},
    {"insert-table", action_insert_table},
    {"insert-matrix", action_insert_matrix},
    {"insert-biblio", action_insert_biblio},
    {"zoom-in", action_zoom_in},
    {"zoom-out", action_zoom_out},
    {"zoom-fit", action_zoom_fit},
    {"zoom-fit-page", action_zoom_fit_page},
    {"zoom-reset", action_zoom_reset},
    {"prev-page", action_prev_page},
    {"next-page", action_next_page},
    {"preview-layout", NULL, "s", "'continuous'", change_preview_layout},
    {"set-theme", NULL, "s", "'follow'", change_theme},
    {"show-recent", action_show_recent},
    {"find", action_find},
    {"find-replace", action_find_replace},
    {"forward-sync", action_forward_sync},
    {"toggle-preview", action_toggle_preview},
    {"toggle-sidebar", action_toggle_sidebar},
    {"toggle-log", action_toggle_log},
    {"refresh-structure", action_refresh_structure},
    {"preferences", action_preferences},
    {"shortcuts", action_shortcuts},
    {"fullscreen", action_fullscreen},
    {"run-bibtex", action_run_bibtex},
    {"run-makeindex", action_run_makeindex},
    {"cleanup", action_cleanup},
    {"stats", action_stats},
    {"open-pdf-external", action_open_pdf_external},
    {"git-status", action_git_status},
    {"git-commit", action_git_commit},
    {"git-pull", action_git_pull},
    {"git-push", action_git_push},
    {"open-menu", action_open_menu},
};

/* ------------------------------------------------------------ key routing */

static gboolean on_window_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                      GdkModifierType state, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return GDK_EVENT_PROPAGATE;
    return silktex_snippets_handle_key(self->snippets, editor, keyval, state);
}

static gboolean on_window_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                       GdkModifierType state, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return GDK_EVENT_PROPAGATE;
    return silktex_snippets_handle_key_release(self->snippets, editor, keyval, state);
}

/* ------------------------------------------------------------ class */

static void silktex_window_dispose(GObject *object)
{
    SilktexWindow *self = SILKTEX_WINDOW(object);

    if (self->compile_timer_id > 0) {
        g_source_remove(self->compile_timer_id);
        self->compile_timer_id = 0;
    }
    if (self->autosave_timer_id > 0) {
        g_source_remove(self->autosave_timer_id);
        self->autosave_timer_id = 0;
    }

    self->current_toast = NULL;

    if (self->compiler) silktex_compiler_stop(self->compiler);
    self->git_branch_label = NULL;
    self->git_repo_label = NULL;
    self->git_list = NULL;
    self->git_commit_message = NULL;
    self->git_dialog = NULL;
    g_clear_pointer(&self->git_status, silktex_git_status_free);
    g_clear_pointer(&self->git_status_message, g_free);
    g_clear_object(&self->compiler);
    g_clear_object(&self->snippets);

    G_OBJECT_CLASS(silktex_window_parent_class)->dispose(object);
}

static void silktex_window_class_init(SilktexWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = silktex_window_dispose;

    gtk_widget_class_set_template_from_resource(widget_class, "/app/silktex/main.ui");

    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, root_toolbar_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, window_title);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, toast_overlay);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, tab_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, tab_bar);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, split_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, editor_paned);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, editor_toolbar_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, editor_bottom_bar);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, preview_toolbar_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, preview_box);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, structure_container);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, page_label);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, preview_status);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_preview);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_sidebar);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_compile);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_menu);
}

static void silktex_window_init(SilktexWindow *self)
{
    g_type_ensure(SILKTEX_TYPE_PREVIEW);
    install_silktex_chrome_css();
    gtk_widget_init_template(GTK_WIDGET(self));

    /* Flat top bars: avoid an extra "step" and shadow between title bar,
     * tab strip, and the split — reads as one continuous header band. */
    if (self->root_toolbar_view) {
        adw_toolbar_view_set_top_bar_style(self->root_toolbar_view, ADW_TOOLBAR_FLAT);
    }
    if (self->editor_toolbar_view) {
        adw_toolbar_view_set_top_bar_style(self->editor_toolbar_view, ADW_TOOLBAR_FLAT);
    }
    if (self->preview_toolbar_view) {
        adw_toolbar_view_set_top_bar_style(self->preview_toolbar_view, ADW_TOOLBAR_FLAT);
    }
    /* Flat bottom bars: same hairline weight as the paned separator (raised
     * toolbars use a heavier top edge that looked bigger than the split). */
    if (self->editor_toolbar_view) {
        adw_toolbar_view_set_bottom_bar_style(self->editor_toolbar_view, ADW_TOOLBAR_FLAT);
    }
    if (self->preview_toolbar_view) {
        adw_toolbar_view_set_bottom_bar_style(self->preview_toolbar_view, ADW_TOOLBAR_FLAT);
    }

    g_action_map_add_action_entries(G_ACTION_MAP(self), win_actions, G_N_ELEMENTS(win_actions),
                                    self);
    update_git_actions(self);

    /* Reflect the persisted theme choice in the "Theme" radio state. */
    GAction *theme_action = g_action_map_lookup_action(G_ACTION_MAP(self), "set-theme");
    if (theme_action) {
        const char *saved = config_get_string("Interface", "theme");
        if (!saved || !*saved) saved = "follow";
        g_simple_action_set_state(G_SIMPLE_ACTION(theme_action), g_variant_new_string(saved));
    }
    apply_theme_from_config();
    install_primary_popover(self);
    g_signal_connect_object(adw_style_manager_get_default(), "notify::dark",
                            G_CALLBACK(on_system_dark_changed), self, G_CONNECT_DEFAULT);

    /* ---- compiler ---- */
    self->compiler = silktex_compiler_new();
    silktex_compiler_apply_config(self->compiler);
    silktex_compiler_start(self->compiler);

    g_signal_connect(self->compiler, "compile-finished", G_CALLBACK(on_compile_finished), self);
    g_signal_connect(self->compiler, "compile-error", G_CALLBACK(on_compile_error), self);

    /* ---- preview ---- */
    self->preview = silktex_preview_new();
    gtk_widget_set_hexpand(GTK_WIDGET(self->preview), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self->preview), TRUE);
    gtk_box_append(self->preview_box, GTK_WIDGET(self->preview));

    if (self->editor_toolbar_view) {
        gtk_widget_set_size_request(GTK_WIDGET(self->editor_toolbar_view), EDITOR_MIN_WIDTH, -1);
    }

    g_signal_connect(self->preview, "notify::page", G_CALLBACK(on_preview_page_changed), self);
    g_signal_connect(self->preview, "notify::n-pages", G_CALLBACK(on_preview_page_changed), self);

    /* ---- structure sidebar ---- */
    self->structure = silktex_structure_new();
    gtk_widget_set_vexpand(GTK_WIDGET(self->structure), TRUE);
    gtk_box_append(self->structure_container, GTK_WIDGET(self->structure));

    /* ---- search bar ----
     *
     * Attach the search overlay as a *top* bar of the editor's ToolbarView
     * so it sits directly under the window tab strip when opened and does not
     * interfere with the bottom toolbar or the compile-log revealer. */
    self->searchbar = silktex_searchbar_new();
    if (self->editor_toolbar_view) {
        adw_toolbar_view_add_top_bar(self->editor_toolbar_view, GTK_WIDGET(self->searchbar));
    }

    /* ---- snippets ---- */
    self->snippets = silktex_snippets_new();
    silktex_snippets_set_modifiers(self->snippets, config_get_string("Snippets", "modifier1"),
                                   config_get_string("Snippets", "modifier2"));
    GtkEventControllerKey *key_ctrl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key_ctrl), GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(key_ctrl));
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_window_key_pressed), self);
    g_signal_connect(key_ctrl, "key-released", G_CALLBACK(on_window_key_released), self);

    g_signal_connect(self->btn_preview, "toggled", G_CALLBACK(on_preview_toggled), self);
    on_preview_toggled(self->btn_preview, self);
    g_signal_connect(self, "notify::width", G_CALLBACK(on_window_width_changed), self);
    g_signal_connect(self->editor_paned, "notify::position",
                     G_CALLBACK(on_editor_paned_position_changed), self);
    g_signal_connect(self->tab_view, "notify::selected-page", G_CALLBACK(on_tab_changed), self);
    g_signal_connect(self->tab_view, "close-page", G_CALLBACK(on_close_page), self);

    self->auto_compile = config_get_boolean("Compile", "auto_compile");
    restart_autosave_timer(self);

    /* ---- compile log panel ----
     *
     * Revealer sits as a *bottom* bar of the editor ToolbarView, just above
     * the editor_bottom_bar.  The toggle that opens / closes it is appended
     * to the end of the bottom bar so it pairs with the code tools on the
     * left. */
    {
        GtkWidget *revealer = gtk_revealer_new();
        gtk_revealer_set_transition_type(GTK_REVEALER(revealer),
                                         GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
        gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
        self->log_revealer = GTK_REVEALER(revealer);

        self->log_buf = gtk_text_buffer_new(NULL);
        GtkWidget *log_tv = gtk_text_view_new_with_buffer(self->log_buf);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(log_tv), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_tv), TRUE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_tv), FALSE);
        gtk_widget_set_vexpand(log_tv, TRUE);
        gtk_widget_set_focusable(log_tv, TRUE);
        self->log_text_view = log_tv;

        GtkWidget *log_scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(log_scroll), 300);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), log_tv);
        gtk_revealer_set_child(GTK_REVEALER(revealer), log_scroll);

        if (self->editor_toolbar_view) {
            adw_toolbar_view_add_bottom_bar(self->editor_toolbar_view, revealer);
        }

        GtkWidget *log_toggle = gtk_toggle_button_new();
        gtk_button_set_icon_name(GTK_BUTTON(log_toggle), "text-x-generic-symbolic");
        gtk_widget_set_tooltip_text(log_toggle, _("Compile Log"));
        gtk_widget_add_css_class(log_toggle, "flat");
        self->log_toggle = GTK_TOGGLE_BUTTON(log_toggle);

        if (self->editor_bottom_bar) {
            gtk_box_append(self->editor_bottom_bar, log_toggle);
        }

        g_object_bind_property(log_toggle, "active", revealer, "reveal-child", G_BINDING_DEFAULT);
        g_signal_connect(log_toggle, "notify::active", G_CALLBACK(on_log_toggle_active), self);
    }

    /* ---- initial tab ---- */
    silktex_window_new_tab(self);
    refresh_git_state(self);

    /* ---- accelerators ---- */
    const char *accels[][2] = {
        {"win.new", "<Control>n"},
        {"win.open", "<Control>o"},
        {"win.save", "<Control>s"},
        {"win.save-as", "<Control><Shift>s"},
        {"win.undo", "<Control>z"},
        {"win.redo", "<Control><Shift>z"},
        {"win.compile", "<Control>Return"},
        {"win.bold", "<Control>b"},
        {"win.italic", "<Control>i"},
        {"win.underline", "<Control>u"},
        {"win.zoom-in", "<Control>plus"},
        {"win.zoom-out", "<Control>minus"},
        {"win.zoom-fit", "<Control>0"},
        {"win.find", "<Control>f"},
        {"win.find-replace", "<Control>h"},
        {"win.forward-sync", "<Control><Shift>f"},
        {"win.toggle-preview", "F9"},
        {"win.toggle-sidebar", "F8"},
        {"win.fullscreen", "F11"},
        {"win.open-menu", "F10"},
        {"win.preferences", "<Control>comma"},
        {"win.shortcuts", "<Control>question"},
        {"win.next-page", "<Control>Page_Down"},
        {"win.prev-page", "<Control>Page_Up"},
        {"app.quit", "<Control>q"},
    };

    GtkApplication *app = GTK_APPLICATION(g_application_get_default());
    for (size_t i = 0; i < G_N_ELEMENTS(accels); i++) {
        const char *accel_list[] = {accels[i][1], NULL};
        gtk_application_set_accels_for_action(app, accels[i][0], accel_list);
    }

    update_page_label(self);
}

/* ------------------------------------------------------------ public */

SilktexWindow *silktex_window_new(AdwApplication *app)
{
    return g_object_new(SILKTEX_TYPE_WINDOW, "application", app, NULL);
}

void silktex_window_open_file(SilktexWindow *self, GFile *file)
{
    g_return_if_fail(SILKTEX_IS_WINDOW(self));
    g_return_if_fail(G_IS_FILE(file));

    SilktexEditor *editor = silktex_editor_new();
    silktex_editor_load_file(editor, file);

    GtkWidget *page_widget = create_editor_page(self, editor);
    AdwTabPage *page = adw_tab_view_append(self->tab_view, page_widget);

    g_autofree char *basename = silktex_editor_get_basename(editor);
    adw_tab_page_set_title(page, basename);
    adw_tab_view_set_selected_page(self->tab_view, page);

    g_object_unref(editor);

    update_window_title(self);
    add_to_recent(file);

    if (self->auto_compile) {
        restart_compile_timer(self);
    }
    refresh_git_state(self);
}

void silktex_window_new_tab(SilktexWindow *self)
{
    g_return_if_fail(SILKTEX_IS_WINDOW(self));

    SilktexEditor *editor = silktex_editor_new();

    g_autofree char *vorlage = g_build_filename(GUMMI_DATA, "templates", "vorlage.tex", NULL);
    if (g_file_test(vorlage, G_FILE_TEST_EXISTS)) {
        g_autofree char *text = NULL;
        if (g_file_get_contents(vorlage, &text, NULL, NULL))
            silktex_editor_set_text(editor, text, -1);
    }

    GtkWidget *page_widget = create_editor_page(self, editor);
    AdwTabPage *page = adw_tab_view_append(self->tab_view, page_widget);

    adw_tab_page_set_title(page, _("Untitled"));
    adw_tab_view_set_selected_page(self->tab_view, page);

    g_object_unref(editor);

    update_window_title(self);
    refresh_git_state(self);
}

SilktexEditor *silktex_window_get_active_editor(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);

    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    return get_editor_for_page(page);
}

SilktexCompiler *silktex_window_get_compiler(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);
    return self->compiler;
}

SilktexPreview *silktex_window_get_preview(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);
    return self->preview;
}

static void on_current_toast_dismissed(AdwToast *toast, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (self->current_toast == toast) self->current_toast = NULL;
}

void silktex_window_show_toast(SilktexWindow *self, const char *message)
{
    g_return_if_fail(SILKTEX_IS_WINDOW(self));

    if (self->current_toast != NULL) {
        adw_toast_dismiss(self->current_toast);
        self->current_toast = NULL;
    }

    AdwToast *toast = adw_toast_new(message);
    adw_toast_set_timeout(toast, 3);
    adw_toast_set_priority(toast, ADW_TOAST_PRIORITY_HIGH);
    g_signal_connect(toast, "dismissed", G_CALLBACK(on_current_toast_dismissed), self);
    self->current_toast = toast;
    adw_toast_overlay_add_toast(self->toast_overlay, toast);
}
