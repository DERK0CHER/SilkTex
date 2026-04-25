/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SilktexWindow — main application window: GObject type, template wiring,
 * timers, tabbed editors, file dialogs, LaTeX actions, preview layout,
 * compiler integration, and the bulk of win.* GActions.
 *
 * Companion translation units (same struct, see window-private.h):
 *   window-git.c           — Git dialog and git-* actions
 *   window-primary-menu.c  — Hamburger menu, theme, recent, shortcuts
 *
 * Editor ↔ tab association: each tab's page child is a GtkScrolledWindow
 * holding the GtkSourceView; g_object_get_data(..., "silktex-editor") stores
 * the SilktexEditor (see silktex_window_editor_for_page).
 */

#include "window-private.h"
#include "prefs.h"
#include "searchbar.h"
#include "snippets.h"
#include "synctex.h"
#include "structure.h"
#include "latex.h"
#include "configfile.h"
#include "constants.h"
#include "style-schemes.h"
#include "utils.h"
#include "i18n.h"

G_DEFINE_FINAL_TYPE (SilktexWindow, silktex_window, ADW_TYPE_APPLICATION_WINDOW)

/* -------------------------------------------------------------------------- */
/* Title, log, theme — shared with menu / git modules via window-private.h */

SilktexEditor *silktex_window_editor_for_page(AdwTabPage *page)
{
    if (page == NULL) return NULL;
    GtkWidget *child = adw_tab_page_get_child(page);
    if (child == NULL) return NULL;
    return g_object_get_data(G_OBJECT(child), "silktex-editor");
}

void silktex_window_update_window_title(SilktexWindow *self)
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

void silktex_window_update_tab_title(SilktexWindow *self, AdwTabPage *page, SilktexEditor *editor)
{
    g_autofree char *basename = silktex_editor_get_basename(editor);
    const char *modified = silktex_editor_get_modified(editor) ? "*" : "";
    g_autofree char *title = g_strdup_printf("%s%s", modified, basename);
    adw_tab_page_set_title(page, title);
}

void silktex_window_update_page_label(SilktexWindow *self)
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

void silktex_window_update_log_panel(SilktexWindow *self)
{
    if (!self->log_buf) return;
    const char *log = silktex_compiler_get_log(self->compiler);
    gtk_text_buffer_set_text(self->log_buf, log ? log : "", -1);
}

/*
 * GtkSourceView scheme id — combines Adwaita dark/light, "lightsout", and
 * optional fixed scheme from preferences (see style-schemes.c).
 */
void silktex_window_apply_theme_to_editor(SilktexEditor *editor)
{
    if (editor != NULL) silktex_editor_set_style_scheme(editor, silktex_resolved_style_scheme_id());
}

void silktex_window_apply_theme_to_all_editors(SilktexWindow *self)
{
    guint n = adw_tab_view_get_n_pages(self->tab_view);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(self->tab_view, i);
        silktex_window_apply_theme_to_editor(silktex_window_editor_for_page(page));
    }
}

void silktex_window_apply_preview_theme(SilktexWindow *self)
{
    if (!self || !self->preview) return;
    const char *mode = config_get_string("Interface", "theme");
    silktex_preview_set_inverted(self->preview, g_strcmp0(mode, "dark") == 0);
}

void silktex_window_focus_active_editor(SilktexWindow *self)
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

/* -------------------------------------------------------------------------- */
/*
 * Compile timer: one-shot after last edit (when auto_compile is on).
 * Autosave timer: periodic save of modified tabs with on-disk paths; also
 * refreshes git status so the dialog stays roughly in sync.
 */

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

void silktex_window_restart_compile_timer(SilktexWindow *self)
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
        SilktexEditor *editor = silktex_window_editor_for_page(page);
        if (editor && silktex_editor_get_modified(editor)) {
            const char *fname = silktex_editor_get_filename(editor);
            if (fname && *fname) {
                GFile *f = g_file_new_for_path(fname);
                silktex_editor_save_file(editor, f, NULL);
                g_object_unref(f);
            }
        }
    }
    silktex_window_git_refresh_state(self);
    return G_SOURCE_CONTINUE;
}

void silktex_window_restart_autosave_timer(SilktexWindow *self)
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

/* -------------------------------------------------------------------------- */
/* Editor / compiler / preview signals */

static void on_editor_changed(SilktexEditor *editor, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    if (page != NULL && silktex_window_editor_for_page(page) == editor) {
        silktex_window_update_tab_title(self, page, editor);
        silktex_window_update_window_title(self);
    }

    if (self->auto_compile) {
        silktex_window_restart_compile_timer(self);
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
    silktex_window_update_log_panel(self);
    if (self->preview_status) gtk_label_set_label(self->preview_status, _("Compiled"));
    if (self->log_toggle) gtk_widget_remove_css_class(GTK_WIDGET(self->log_toggle), "error");
}

static void on_compile_error(SilktexCompiler *compiler, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_window_show_toast(self, _("Compilation error — see compile log"));
    silktex_window_update_log_panel(self);
    if (self->preview_status) gtk_label_set_label(self->preview_status, _("Compile error"));
    if (self->log_toggle) gtk_widget_add_css_class(GTK_WIDGET(self->log_toggle), "error");

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
    silktex_window_update_page_label(SILKTEX_WINDOW(ud));
}

static gboolean on_editor_scroll_zoom(GtkEventControllerScroll *ctrl, double dx, double dy,
                                      gpointer user_data);

/* -------------------------------------------------------------------------- */
/* Tab page widget — scrollable editor view, ref stored as object data */

GtkWidget *silktex_window_create_editor_page(SilktexWindow *self, SilktexEditor *editor)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), silktex_editor_get_view(editor));
    /* Place vertical scrollbar to the left of the text (LTR: GTK_CORNER_TOP_RIGHT). */
    gtk_scrolled_window_set_placement(GTK_SCROLLED_WINDOW(scrolled), GTK_CORNER_TOP_RIGHT);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_editor_scroll_zoom), self);
    gtk_widget_add_controller(scrolled, scroll);

    g_object_set_data_full(G_OBJECT(scrolled), "silktex-editor", g_object_ref(editor),
                           g_object_unref);

    g_signal_connect(editor, "changed", G_CALLBACK(on_editor_changed), self);

    silktex_editor_apply_settings(editor);
    silktex_window_apply_theme_to_editor(editor);

    return scrolled;
}

static gboolean on_editor_scroll_zoom(GtkEventControllerScroll *ctrl, double dx, double dy,
                                      gpointer user_data)
{
    (void)dx;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if ((state & GDK_CONTROL_MASK) == 0) return GDK_EVENT_PROPAGATE;
    if (dy < 0)
        gtk_widget_activate_action(GTK_WIDGET(self), "win.zoom-in", NULL);
    else if (dy > 0)
        gtk_widget_activate_action(GTK_WIDGET(self), "win.zoom-out", NULL);
    return GDK_EVENT_STOP;
}

/* -------------------------------------------------------------------------- */
/* File dialogs — GtkFileDialog async callbacks */

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
            silktex_window_update_tab_title(self, page, editor);
            silktex_window_update_window_title(self);
            add_to_recent(file);
            silktex_window_git_refresh_state(self);
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
            silktex_window_update_tab_title(self, page, editor);
            silktex_window_update_window_title(self);
            add_to_recent(file);
            silktex_window_git_refresh_state(self);
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

/* -------------------------------------------------------------------------- */
/* Export PDF — copy from compiler output path to user-chosen location */

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

/* -------------------------------------------------------------------------- */
/* Close current tab (unsaved guard is on_close_page) */

static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    if (page != NULL) adw_tab_view_close_page(self->tab_view, page);
}

/* -------------------------------------------------------------------------- */
/* Buffer operations and compile */

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

/* -------------------------------------------------------------------------- */
/* LaTeX insertion — delegates to latex.c */

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

/* -------------------------------------------------------------------------- */
/* PDF preview — zoom, page flip, continuous vs single-page layout */

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

/* -------------------------------------------------------------------------- */
/* Search / replace overlay and SyncTeX forward jump */

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

/* -------------------------------------------------------------------------- */
/* Chrome toggles, preferences, fullscreen, outline refresh */

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
    silktex_prefs_set_apply_callback(prefs, silktex_window_on_prefs_apply, self);
    silktex_prefs_set_snippets(prefs, self->snippets);
    silktex_prefs_present(prefs, GTK_WINDOW(self));
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
        silktex_window_focus_active_editor(self);
    }
}

static void action_refresh_structure(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (self->structure) silktex_structure_refresh(self->structure);
}

/* -------------------------------------------------------------------------- */
/* Auxiliary build steps and housekeeping */

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

/* -------------------------------------------------------------------------- */
/* Called when the preferences dialog applies — all tabs + running compiler */

void silktex_window_on_prefs_apply(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    guint n = adw_tab_view_get_n_pages(self->tab_view);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(self->tab_view, i);
        SilktexEditor *editor = silktex_window_editor_for_page(page);
        if (editor) {
            silktex_editor_apply_settings(editor);
            silktex_window_apply_theme_to_editor(editor);
        }
    }
    config_save();
    self->auto_compile = config_get_boolean("Compile", "auto_compile");
    silktex_compiler_apply_config(self->compiler);
    silktex_window_restart_autosave_timer(self);

    /* Snippet shortcuts may have changed; push the new modifier pair
     * through to the running engine so shortcuts update immediately. */
    if (self->snippets) {
        silktex_snippets_set_modifiers(self->snippets, config_get_string("Snippets", "modifier1"),
                                       config_get_string("Snippets", "modifier2"));
    }
}

/* -------------------------------------------------------------------------- */
/*
 * Editor | preview split (GtkPaned). We clamp the handle so neither pane
 * drops below SILKTEX_*_MIN_WIDTH. Narrow window: auto-hide preview and
 * restore when widening (preview_narrow / preview_auto_collapsed).
 */

static int clamp_editor_pane_start(int w, int pos)
{
    if (w < 1) return pos;
    int max_start = w - SILKTEX_PREVIEW_PANE_MIN_WIDTH;
    if (max_start < SILKTEX_EDITOR_MIN_WIDTH) max_start = SILKTEX_EDITOR_MIN_WIDTH;
    if (pos < SILKTEX_EDITOR_MIN_WIDTH) return SILKTEX_EDITOR_MIN_WIDTH;
    if (pos > max_start) return max_start;
    return pos;
}

void silktex_window_apply_editor_paned_half_split(SilktexWindow *self)
{
    if (self->editor_paned == NULL) return;

    int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
    if (w < 1) return;

    int half = w / 2;
    int max_start = w - SILKTEX_PREVIEW_PANE_MIN_WIDTH;
    if (max_start < SILKTEX_EDITOR_MIN_WIDTH) max_start = SILKTEX_EDITOR_MIN_WIDTH;
    if (half > max_start) half = max_start;
    if (half < SILKTEX_EDITOR_MIN_WIDTH) half = SILKTEX_EDITOR_MIN_WIDTH;
    if (half > max_start) half = (SILKTEX_EDITOR_MIN_WIDTH + max_start) / 2;

    self->preview_pane_silence = TRUE;
    gtk_paned_set_position(self->editor_paned, half);
    self->preview_pane_silence = FALSE;
    self->preview_pane_pos = gtk_paned_get_position(self->editor_paned);
    self->preview_pane_ratio = (double)self->preview_pane_pos / (double)w;
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
        silktex_window_apply_editor_paned_half_split(self);
        return;
    }

    int target = self->preview_pane_pos;
    if (self->preview_pane_ratio > 0.0 && self->preview_pane_ratio < 1.0)
        target = (int)(self->preview_pane_ratio * (double)w);
    int pos = clamp_editor_pane_start(w, target);
    self->preview_pane_silence = TRUE;
    gtk_paned_set_position(self->editor_paned, pos);
    self->preview_pane_silence = FALSE;
    self->preview_pane_pos = gtk_paned_get_position(self->editor_paned);
    self->preview_pane_ratio = (double)self->preview_pane_pos / (double)w;
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
            self->preview_pane_ratio = (double)self->preview_pane_pos / (double)w;
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

    /* Threshold chosen so side-by-side editor + PDF stays usable on laptops. */
    int width = gtk_widget_get_width(GTK_WIDGET(self));
    gboolean narrow = width > 0 && width < 1024;
    if (narrow == self->preview_narrow) return;

    self->preview_narrow = narrow;

    if (narrow && gtk_toggle_button_get_active(self->btn_preview) && self->editor_paned) {
        int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
        if (w > 0) {
            int collapsed_pos = clamp_editor_pane_start(w, w - 340);
            self->preview_pane_silence = TRUE;
            gtk_paned_set_position(self->editor_paned, collapsed_pos);
            self->preview_pane_silence = FALSE;
            self->preview_pane_pos = collapsed_pos;
            self->preview_pane_ratio = (double)collapsed_pos / (double)w;
            self->preview_pane_restorable = TRUE;
        }
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
    if (w > 0) self->preview_pane_ratio = (double)self->preview_pane_pos / (double)w;
    self->preview_pane_restorable = TRUE;
}

static void on_tab_changed(AdwTabView *view, GParamSpec *pspec, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_window_update_window_title(self);

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        silktex_searchbar_set_editor(self->searchbar, editor);
        silktex_structure_set_editor(self->structure, editor);
        if (self->auto_compile) silktex_window_restart_compile_timer(self);
    }
    silktex_window_git_refresh_state(self);
}

/*
 * AdwTabView "close-page" is async when we show a save confirmation — we
 * must call adw_tab_view_close_page_finish with accept/reject.
 */

typedef struct {
    AdwTabView *view;
    AdwTabPage *page;
    SilktexWindow *win;
} ClosePageData;

static void on_close_dialog_response(AdwAlertDialog *dialog, const char *response,
                                     gpointer user_data)
{
    ClosePageData *d = user_data;
    SilktexEditor *editor = silktex_window_editor_for_page(d->page);

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
    SilktexEditor *editor = silktex_window_editor_for_page(page);

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

/*
 * Core win.* actions. Theme, recent, shortcuts, open-menu, and git-* are
 * registered separately in window-primary-menu.c and window-git.c so this
 * table stays maintainable.
 */

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
    {"find", action_find},
    {"find-replace", action_find_replace},
    {"forward-sync", action_forward_sync},
    {"toggle-preview", action_toggle_preview},
    {"toggle-sidebar", action_toggle_sidebar},
    {"toggle-log", action_toggle_log},
    {"refresh-structure", action_refresh_structure},
    {"preferences", action_preferences},
    {"fullscreen", action_fullscreen},
    {"run-bibtex", action_run_bibtex},
    {"run-makeindex", action_run_makeindex},
    {"cleanup", action_cleanup},
    {"stats", action_stats},
    {"open-pdf-external", action_open_pdf_external},
};

/*
 * Snippet expansion — capture phase so we see keys before the source view
 * consumes them (controller attached to the toplevel window).
 */

static gboolean on_window_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                      GdkModifierType state, gpointer user_data)
{
    (void)ctrl;
    (void)keycode;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return GDK_EVENT_PROPAGATE;
    return silktex_snippets_handle_key(self->snippets, editor, keyval, state);
}

static gboolean on_window_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                                       GdkModifierType state, gpointer user_data)
{
    (void)ctrl;
    (void)keycode;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return GDK_EVENT_PROPAGATE;
    return silktex_snippets_handle_key_release(self->snippets, editor, keyval, state);
}

/* -------------------------------------------------------------------------- */
/* GObject / GtkWidget class */

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
    /* Git dialog widgets may still exist — null pointers so async callbacks
     * never dereference freed children if dispose races a GTask completion. */
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
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_export_doc);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_git_menu);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_export);
}

/*
 * Application-wide tweaks: thin paned separator, sidebar depth, flat bottom
 * toolbars, compile log revealer without extra borders. Loaded once per process.
 */

void silktex_window_install_chrome_css(void)
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
        "}"
        ".silktex-sidebar-pane {"
        "  box-shadow: 1px 0 0 0 @borders, 1px 0 4px alpha(black, 0.2);"
        "}"
        "box.toolbar.silktex-bottom-toolbar, box.silktex-bottom-toolbar {"
        "  border: none;"
        "  box-shadow: none;"
        "  outline: none;"
        "}"
        "box.silktex-bottom-toolbar > separator, box.toolbar.silktex-bottom-toolbar > separator, "
        "box.silktex-bottom-toolbar separator, box.toolbar.silktex-bottom-toolbar separator {"
        "  min-width: 0;"
        "  min-height: 0;"
        "  background: transparent;"
        "  color: transparent;"
        "  margin: 0;"
        "  padding: 0;"
        "  border: none;"
        "}"
        "revealer.silktex-compile-log {"
        "  box-shadow: none;"
        "  border: none;"
        "}"
        "revealer.silktex-compile-log > * {"
        "  box-shadow: none;"
        "  border: none;"
        "}"
        "togglebutton.error {"
        "  color: @error_color;"
        "}"
        ".silktex-preview-scroller scrollbar slider {"
        "  background-color: alpha(0.5, 0.45);"
        "}"
        ".silktex-preview-scroller scrollbar {"
        "  background-color: alpha(0.5, 0.15);"
        "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/*
 * Init order matters: template children first, then actions (menu + git add
 * more entries), then theme state, then compiler/preview/search, then signals.
 */

static void silktex_window_init(SilktexWindow *self)
{
    g_type_ensure(SILKTEX_TYPE_PREVIEW);
    silktex_window_install_chrome_css();
    gtk_widget_init_template(GTK_WIDGET(self));

    gtk_widget_set_size_request(GTK_WIDGET(self), SILKTEX_WINDOW_MIN_WIDTH, SILKTEX_WINDOW_MIN_HEIGHT);

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
    silktex_window_register_menu_actions(self);
    silktex_window_git_register_actions(self);
    silktex_window_git_update_actions(self);

    /* Reflect the persisted theme choice in the "Theme" radio state. */
    GAction *theme_action = g_action_map_lookup_action(G_ACTION_MAP(self), "set-theme");
    if (theme_action) {
        const char *saved = config_get_string("Interface", "theme");
        if (!saved || !*saved) saved = "follow";
        g_simple_action_set_state(G_SIMPLE_ACTION(theme_action), g_variant_new_string(saved));
    }
    silktex_window_apply_theme_from_config();
    silktex_window_install_primary_menu(self);
    silktex_window_connect_theme_follow(self);

    {
        GtkIconTheme *it = gtk_icon_theme_get_for_display(gdk_display_get_default());
        const char *const export_icons[] = {"arrow-into-box-symbolic", "document-save-symbolic",
                                            "document-export-symbolic", "folder-download-symbolic",
                                            NULL};
        const char *const git_icons[] = {"branch-arrow-symbolic", "vcs-git-symbolic",
                                         "version-control-symbolic", "folder-symbolic", NULL};

        const char *export_icon = export_icons[1];
        for (int i = 0; export_icons[i]; i++) {
            if (gtk_icon_theme_has_icon(it, export_icons[i])) {
                export_icon = export_icons[i];
                break;
            }
        }
        const char *git_icon = git_icons[3];
        for (int i = 0; git_icons[i]; i++) {
            if (gtk_icon_theme_has_icon(it, git_icons[i])) {
                git_icon = git_icons[i];
                break;
            }
        }

        if (self->btn_export_doc) gtk_button_set_icon_name(self->btn_export_doc, export_icon);
        if (self->btn_export) gtk_button_set_icon_name(self->btn_export, export_icon);
        if (self->btn_git_menu) gtk_menu_button_set_icon_name(self->btn_git_menu, git_icon);
    }

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
    silktex_window_apply_preview_theme(self);

    if (self->editor_toolbar_view) {
        gtk_widget_set_size_request(GTK_WIDGET(self->editor_toolbar_view), SILKTEX_EDITOR_MIN_WIDTH, -1);
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
    silktex_window_restart_autosave_timer(self);

    /* ---- compile log panel ----
     *
     * Revealer sits as a *bottom* bar of the editor ToolbarView, just above
     * the editor_bottom_bar.  The toggle that opens / closes it is appended
     * to the end of the bottom bar so it pairs with the code tools on the
     * left. */
    {
        GtkWidget *revealer = gtk_revealer_new();
        gtk_widget_add_css_class(revealer, "silktex-compile-log");
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

        GtkWidget *log_toggle = gtk_toggle_button_new_with_label(_("Log"));
        gtk_widget_set_tooltip_text(log_toggle, _("Log"));
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
    silktex_window_git_refresh_state(self);

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

    silktex_window_update_page_label(self);
}

/* -------------------------------------------------------------------------- */
/* Public API (window.h) */

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

    GtkWidget *page_widget = silktex_window_create_editor_page(self, editor);
    AdwTabPage *page = adw_tab_view_append(self->tab_view, page_widget);

    g_autofree char *basename = silktex_editor_get_basename(editor);
    adw_tab_page_set_title(page, basename);
    adw_tab_view_set_selected_page(self->tab_view, page);

    g_object_unref(editor);

    silktex_window_update_window_title(self);
    add_to_recent(file);

    if (self->auto_compile) {
        silktex_window_restart_compile_timer(self);
    }
    silktex_window_git_refresh_state(self);
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

    GtkWidget *page_widget = silktex_window_create_editor_page(self, editor);
    AdwTabPage *page = adw_tab_view_append(self->tab_view, page_widget);

    adw_tab_page_set_title(page, _("Untitled"));
    adw_tab_view_set_selected_page(self->tab_view, page);

    g_object_unref(editor);

    silktex_window_update_window_title(self);
    silktex_window_git_refresh_state(self);
}

SilktexEditor *silktex_window_get_active_editor(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);

    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    return silktex_window_editor_for_page(page);
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

/*
 * Replace any visible toast — avoids stacking multiple AdwToast overlays
 * for rapid-fire errors (e.g. compile retries).
 */

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
