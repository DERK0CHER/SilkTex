/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window-private.h"
#include "collab.h"
#include "cmdpalette.h"
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

#ifndef SILKTEX_DATA
#define SILKTEX_DATA "/usr/share/silktex"
#endif

G_DEFINE_FINAL_TYPE (SilktexWindow, silktex_window, ADW_TYPE_APPLICATION_WINDOW)

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

static gboolean on_compile_timer(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    self->compile_timer_id = 0;

    if (self->auto_compile) {
        SilktexEditor *editor = silktex_window_get_active_editor(self);
        if (editor != NULL) {
            /* Snapshot on the UI thread — compile worker must not touch GtkTextBuffer. */
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
    if (self->log_scroll) gtk_widget_remove_css_class(GTK_WIDGET(self->log_scroll), "error");
    if (self->btn_compile) gtk_widget_remove_css_class(GTK_WIDGET(self->btn_compile), "error");
}

static void on_compile_error(SilktexCompiler *compiler, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_window_show_toast(self, _("Compilation error — see compile log"));
    silktex_window_update_log_panel(self);
    if (self->preview_status) gtk_label_set_label(self->preview_status, _("Compile error"));
    if (self->log_scroll) gtk_widget_add_css_class(GTK_WIDGET(self->log_scroll), "error");
    if (self->btn_compile) gtk_widget_add_css_class(GTK_WIDGET(self->btn_compile), "error");

    /* Compiler restored last-good PDF on failure; load it if it exists. */
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

static void on_preview_zoom_changed(GObject *p, GParamSpec *ps, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (!self->preview) return;

    SilktexPreviewZoomMode mode = silktex_preview_get_zoom_mode(self->preview);

    GAction *fit_width_action = g_action_map_lookup_action(G_ACTION_MAP(self), "zoom-fit");
    if (fit_width_action) {
        g_simple_action_set_state(G_SIMPLE_ACTION(fit_width_action),
                                  g_variant_new_boolean(mode == SILKTEX_PREVIEW_ZOOM_FIT_WIDTH));
    }

    GAction *fit_page_action = g_action_map_lookup_action(G_ACTION_MAP(self), "zoom-fit-page");
    if (fit_page_action) {
        g_simple_action_set_state(G_SIMPLE_ACTION(fit_page_action),
                                  g_variant_new_boolean(mode == SILKTEX_PREVIEW_ZOOM_FIT_PAGE));
    }
}

static gboolean on_editor_scroll_zoom(GtkEventControllerScroll *ctrl, double dx, double dy,
                                      gpointer user_data);

GtkWidget *silktex_window_create_editor_page(SilktexWindow *self, SilktexEditor *editor)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scrolled, "silktex-editor-scroller");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), silktex_editor_get_view(editor));
    /* GTK_CORNER_TOP_RIGHT places the scrollbar on the left in LTR layouts. */
    gtk_scrolled_window_set_placement(GTK_SCROLLED_WINDOW(scrolled), GTK_CORNER_TOP_RIGHT);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_editor_scroll_zoom), self);
    gtk_widget_add_controller(scrolled, scroll);

    g_object_set_data_full(G_OBJECT(scrolled), "silktex-editor", g_object_ref(editor),
                           g_object_unref);

    g_signal_connect(editor, "changed", G_CALLBACK(on_editor_changed), self);
    silktex_collab_connect_editor(editor);

    silktex_editor_apply_settings(editor);
    silktex_window_apply_theme_to_editor(editor);

    return scrolled;
}

static gboolean on_editor_scroll_zoom(GtkEventControllerScroll *ctrl, double dx, double dy,
                                      gpointer user_data)
{
    (void)dx;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if ((state & GDK_CONTROL_MASK) == 0) return GDK_EVENT_PROPAGATE;
    if (dy < 0)
        gtk_widget_activate_action(GTK_WIDGET(self), "win.editor-zoom-in", NULL);
    else if (dy > 0)
        gtk_widget_activate_action(GTK_WIDGET(self), "win.editor-zoom-out", NULL);
    return GDK_EVENT_STOP;
}

static gboolean on_preview_scroll_zoom(GtkEventControllerScroll *ctrl, double dx, double dy,
                                       gpointer user_data)
{
    (void)dx;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if ((state & GDK_CONTROL_MASK) == 0) return GDK_EVENT_PROPAGATE;
    if (dy < 0)
        gtk_widget_activate_action(GTK_WIDGET(self), "win.zoom-in", NULL);
    else if (dy > 0)
        gtk_widget_activate_action(GTK_WIDGET(self), "win.zoom-out", NULL);
    return GDK_EVENT_STOP;
}

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

static void set_last_tex_save_dir_from_file(SilktexWindow *self, GFile *file);
static GFile *build_default_save_folder(SilktexWindow *self);

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

    /* GtkFileDialog has no auto-suffix API — append .tex if the user typed a bare name. */
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
            set_last_tex_save_dir_from_file(self, file);
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

static void set_last_tex_save_dir_from_file(SilktexWindow *self, GFile *file)
{
    if (!self || !file) return;
    g_autofree char *path = g_file_get_path(file);
    if (!path || !*path) return;
    g_autofree char *dir = g_path_get_dirname(path);
    if (!dir || !*dir) return;
    g_free(self->last_tex_save_dir);
    self->last_tex_save_dir = g_strdup(dir);
}

static GFile *build_default_save_folder(SilktexWindow *self)
{
    if (self && self->last_tex_save_dir && *self->last_tex_save_dir &&
        g_file_test(self->last_tex_save_dir, G_FILE_TEST_IS_DIR))
        return g_file_new_for_path(self->last_tex_save_dir);

    const char *downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (downloads && *downloads && g_file_test(downloads, G_FILE_TEST_IS_DIR))
        return g_file_new_for_path(downloads);

    const char *home = g_get_home_dir();
    if (home && *home) return g_file_new_for_path(home);
    return NULL;
}

static void configure_tex_save_dialog(GtkFileDialog *dialog, SilktexWindow *self,
                                      SilktexEditor *editor)
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
    g_autoptr(GFile) folder = build_default_save_folder(self);
    if (folder) gtk_file_dialog_set_initial_folder(dialog, folder);

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
            set_last_tex_save_dir_from_file(self, file);
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
        configure_tex_save_dialog(dialog, self, editor);
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
    configure_tex_save_dialog(dialog, self, editor);
    gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_save_response, self);
}

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
    g_autoptr(GFile) folder = build_default_save_folder(self);
    if (folder) gtk_file_dialog_set_initial_folder(dlg, folder);
    gtk_file_dialog_save(dlg, GTK_WINDOW(self), NULL, on_export_response, self);
}

static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    if (page != NULL) adw_tab_view_close_page(self->tab_view, page);
}

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
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexPreview *preview = self->preview;
    silktex_preview_toggle_zoom_fit_width(preview);

    gboolean active = (silktex_preview_get_zoom_mode(preview) == SILKTEX_PREVIEW_ZOOM_FIT_WIDTH);
    g_simple_action_set_state(a, g_variant_new_boolean(active));

    GAction *fit_page_action = g_action_map_lookup_action(G_ACTION_MAP(self), "zoom-fit-page");
    if (fit_page_action) {
        g_simple_action_set_state(G_SIMPLE_ACTION(fit_page_action), g_variant_new_boolean(FALSE));
    }
}
static void action_zoom_fit_page(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    SilktexPreview *preview = self->preview;
    silktex_preview_toggle_zoom_fit_page(preview);

    gboolean active = (silktex_preview_get_zoom_mode(preview) == SILKTEX_PREVIEW_ZOOM_FIT_PAGE);
    g_simple_action_set_state(a, g_variant_new_boolean(active));

    GAction *fit_width_action = g_action_map_lookup_action(G_ACTION_MAP(self), "zoom-fit");
    if (fit_width_action) {
        g_simple_action_set_state(G_SIMPLE_ACTION(fit_width_action), g_variant_new_boolean(FALSE));
    }
}
static void action_zoom_reset(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_preview_set_zoom(SILKTEX_WINDOW(ud)->preview, 1.0);
}
static void action_editor_zoom_in(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_zoom_in(e);
}
static void action_editor_zoom_out(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_zoom_out(e);
}
static void action_editor_zoom_reset(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexEditor *e = silktex_window_get_active_editor(SILKTEX_WINDOW(ud));
    if (e) silktex_editor_zoom_reset(e);
}
static void action_command_palette(GSimpleAction *a, GVariant *p, gpointer ud)
{
    silktex_cmd_palette_show(GTK_WIDGET(ud));
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

static void action_find(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (silktex_searchbar_is_open(self->searchbar)) {
        silktex_searchbar_close(self->searchbar);
        return;
    }
    SilktexEditor *e = silktex_window_get_active_editor(self);
    if (e) silktex_searchbar_set_editor(self->searchbar, e);
    silktex_searchbar_open(self->searchbar, FALSE);
}
static void action_find_replace(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (silktex_searchbar_is_open(self->searchbar)) {
        silktex_searchbar_close(self->searchbar);
        return;
    }
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
    if (!silktex_synctex_forward(e, self->preview, pdf)) {
        if (g_find_program_in_path("synctex") == NULL) {
            silktex_window_show_toast(self, _("SyncTeX: 'synctex' command not found in PATH"));
        } else if (!config_get_boolean("Compile", "synctex")) {
            silktex_window_show_toast(
                self, _("SyncTeX: enable in Preferences -> Compile, then recompile"));
        } else {
            silktex_window_show_toast(self, _("SyncTeX: no mapping yet, recompile document"));
        }
    }
}

static void on_preview_inverse_sync_requested(SilktexPreview *preview, int page, double x, double y,
                                              gpointer user_data)
{
    (void)preview;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return;
    const char *pdf = silktex_editor_get_pdffile(editor);
    if (!pdf) return;
    if (!silktex_synctex_inverse(editor, pdf, page, x, y)) {
        if (g_find_program_in_path("synctex") == NULL) {
            silktex_window_show_toast(self, _("SyncTeX: 'synctex' command not found in PATH"));
        } else if (!config_get_boolean("Compile", "synctex")) {
            silktex_window_show_toast(
                self, _("SyncTeX: enable in Preferences -> Compile, then recompile"));
        } else {
            silktex_window_show_toast(self, _("SyncTeX inverse: no mapping at clicked position"));
        }
    }
}

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
    /* AdwDialog releases its floating ref on close — build fresh each time. */
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
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (!self->log_scroll) return;
    gboolean vis = gtk_widget_get_visible(self->log_scroll);
    gtk_widget_set_visible(self->log_scroll, !vis);
}

static void on_log_scroll_visible_changed(GtkWidget *widget, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (gtk_widget_get_visible(widget)) {
        if (self->log_text_view) {
            gtk_widget_grab_focus(self->log_text_view);
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(self->log_buf, &end);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(self->log_text_view), &end, 0.0, FALSE, 0.0, 1.0);
        }
    } else {
        silktex_window_focus_active_editor(self);
    }
}

static void on_tools_split_toggle_active(GtkToggleButton *button, GParamSpec *pspec,
                                         gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    gtk_button_set_icon_name(GTK_BUTTON(button),
                             active ? "go-previous-symbolic" : "go-next-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(button),
                                active ? _("Hide extra tools") : _("Show extra tools"));
}

static void action_refresh_structure(GSimpleAction *a, GVariant *p, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (self->structure) silktex_structure_refresh(self->structure);
}

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

    if (self->snippets) {
        silktex_snippets_set_modifiers(self->snippets, config_get_string("Snippets", "modifier1"),
                                       config_get_string("Snippets", "modifier2"));
    }
}

void silktex_window_apply_editor_paned_half_split(SilktexWindow *self)
{
    if (!self->editor_paned) return;
    int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
    gtk_paned_set_position(self->editor_paned, w > 0 ? w / 2 : 400);
    gtk_widget_set_visible(GTK_WIDGET(self->preview_toolbar_view), TRUE);
    if (self->btn_preview)
        gtk_toggle_button_set_active(self->btn_preview, TRUE);
}

static void on_preview_toggled(GtkToggleButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    gboolean visible = gtk_toggle_button_get_active(button);

    gtk_widget_set_visible(GTK_WIDGET(self->preview_toolbar_view), visible);
    if (visible && self->editor_paned) {
        int w = gtk_widget_get_width(GTK_WIDGET(self->editor_paned));
        if (w > 0 && gtk_paned_get_position(self->editor_paned) >= w - 1)
            gtk_paned_set_position(self->editor_paned, w / 2);
    }

    gtk_button_set_icon_name(GTK_BUTTON(button),
                             visible ? "view-dual-symbolic" : "view-continuous-symbolic");
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

    if (!self->editor_paned || !self->btn_preview) return;

    if (narrow) {
        self->preview_auto_collapsed = gtk_toggle_button_get_active(self->btn_preview);
        gtk_toggle_button_set_active(self->btn_preview, FALSE);
    } else if (self->preview_auto_collapsed) {
        self->preview_auto_collapsed = FALSE;
        gtk_toggle_button_set_active(self->btn_preview, TRUE);
    }
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
    {"zoom-fit", action_zoom_fit, NULL, "false", NULL},
    {"zoom-fit-page", action_zoom_fit_page, NULL, "false", NULL},
    {"zoom-reset", action_zoom_reset},
    {"editor-zoom-in", action_editor_zoom_in},
    {"editor-zoom-out", action_editor_zoom_out},
    {"editor-zoom-reset", action_editor_zoom_reset},
    {"command-palette", action_command_palette},
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

/* Key controller runs in capture phase so snippets see keys before GtkSourceView. */
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
    g_clear_pointer(&self->last_tex_save_dir, g_free);
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
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, log_paned);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, editor_toolbar_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, editor_bottom_bar);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, preview_toolbar_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, preview_box);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, structure_container);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, page_label);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_preview);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_sidebar);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_compile);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_tools_toggle);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_menu);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_git_menu);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_collab);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_save);
}

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
        "  box-shadow: -1px 0 0 0 @borders, -1px 0 4px alpha(black, 0.2);"
        "}"
        "revealer.silktex-compile-log {"
        "  box-shadow: none;"
        "  border: none;"
        "}"
        "revealer.silktex-compile-log > * {"
        "  box-shadow: none;"
        "  border: none;"
        "}"
        "revealer.silktex-compile-log.error {"
        "  outline: 2px solid @error_color;"
        "  outline-offset: -2px;"
        "}"
        "togglebutton.error {"
        "  color: @error_color;"
        "}"
        /* Remove top/bottom undershoot and overshoot shadows from the editor scroll pane. */
        ".silktex-editor-scroller undershoot.top,"
        ".silktex-editor-scroller undershoot.bottom,"
        ".silktex-editor-scroller overshoot.top,"
        ".silktex-editor-scroller overshoot.bottom {"
        "  box-shadow: none;"
        "  background: none;"
        "}"
        /* Push toasts above the bottom action bar so they don't cover the Log button. */
        "toastoverlay .toast {"
        "  margin-bottom: 52px;"
        "}"
        ".silktex-preview-scroller scrollbar slider {"
        "  background-color: rgba(40, 40, 40, 0.55);"
        "}"
        ".silktex-preview-scroller scrollbar {"
        "  background-color: rgba(40, 40, 40, 0.15);"
        "}"
        /* Collab: online icon gets accent colour */
        ".collab-online-icon {"
        "  color: @accent_color;"
        "}"
        /* Collab: peer-count badge pill */
        ".collab-badge {"
        "  background-color: @accent_bg_color;"
        "  color: @accent_fg_color;"
        "  border-radius: 100px;"
        "  padding: 1px 4px;"
        "  min-width: 14px;"
        "  font-size: 8pt;"
        "  font-weight: bold;"
        "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void silktex_window_init(SilktexWindow *self)
{
    g_type_ensure(SILKTEX_TYPE_PREVIEW);
    silktex_window_install_chrome_css();
    gtk_widget_init_template(GTK_WIDGET(self));

    if (self->root_toolbar_view) {
        adw_toolbar_view_set_top_bar_style(self->root_toolbar_view, ADW_TOOLBAR_FLAT);
        adw_toolbar_view_set_bottom_bar_style(self->root_toolbar_view, ADW_TOOLBAR_RAISED_BORDER);
    }
    if (self->editor_toolbar_view) {
        adw_toolbar_view_set_top_bar_style(self->editor_toolbar_view, ADW_TOOLBAR_FLAT);
    }
    if (self->preview_toolbar_view) {
        adw_toolbar_view_set_top_bar_style(self->preview_toolbar_view, ADW_TOOLBAR_FLAT);
    }
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
    silktex_collab_setup_window(self);
    silktex_window_git_update_actions(self);

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
        gtk_icon_theme_add_resource_path(it, "/app/silktex/icons");
        /* folder-symbolic matches as fallback on some themes, producing wrong icon. */
        const char *const git_icons[] = {"git-symbolic", "vcs-git-symbolic",
                                         "branch-arrow-symbolic", "version-control-symbolic", NULL};

        const char *git_icon = "git-symbolic";
        for (int i = 0; git_icons[i]; i++) {
            if (gtk_icon_theme_has_icon(it, git_icons[i])) {
                git_icon = git_icons[i];
                break;
            }
        }

        if (self->btn_git_menu) gtk_menu_button_set_icon_name(self->btn_git_menu, git_icon);
    }

    self->compiler = silktex_compiler_new();
    silktex_compiler_apply_config(self->compiler);
    silktex_compiler_start(self->compiler);

    g_signal_connect(self->compiler, "compile-finished", G_CALLBACK(on_compile_finished), self);
    g_signal_connect(self->compiler, "compile-error", G_CALLBACK(on_compile_error), self);

    self->preview = silktex_preview_new();
    gtk_widget_set_hexpand(GTK_WIDGET(self->preview), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self->preview), TRUE);
    gtk_box_append(self->preview_box, GTK_WIDGET(self->preview));
    silktex_window_apply_preview_theme(self);

    if (self->editor_paned)
        gtk_paned_set_position(self->editor_paned, 400);

    g_signal_connect(self->preview, "notify::page", G_CALLBACK(on_preview_page_changed), self);
    g_signal_connect(self->preview, "notify::n-pages", G_CALLBACK(on_preview_page_changed), self);
    g_signal_connect(self->preview, "notify::zoom", G_CALLBACK(on_preview_zoom_changed), self);
    g_signal_connect(self->preview, "inverse-sync-requested",
                     G_CALLBACK(on_preview_inverse_sync_requested), self);

    {
        GtkEventController *preview_scroll =
            gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
        g_signal_connect(preview_scroll, "scroll", G_CALLBACK(on_preview_scroll_zoom), self);
        gtk_widget_add_controller(GTK_WIDGET(self->preview), preview_scroll);
    }

    self->structure = silktex_structure_new();
    gtk_widget_set_vexpand(GTK_WIDGET(self->structure), TRUE);
    gtk_box_append(self->structure_container, GTK_WIDGET(self->structure));

    /* Searchbar as top bar keeps it under the tab strip, away from the log revealer. */
    self->searchbar = silktex_searchbar_new();
    if (self->editor_toolbar_view) {
        adw_toolbar_view_add_top_bar(self->editor_toolbar_view, GTK_WIDGET(self->searchbar));
    }

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
    g_signal_connect(self->tab_view, "notify::selected-page", G_CALLBACK(on_tab_changed), self);
    g_signal_connect(self->tab_view, "close-page", G_CALLBACK(on_close_page), self);

    self->auto_compile = config_get_boolean("Compile", "auto_compile");
    silktex_window_restart_autosave_timer(self);

    {
        self->log_buf = gtk_text_buffer_new(NULL);
        GtkWidget *log_tv = gtk_text_view_new_with_buffer(self->log_buf);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(log_tv), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_tv), TRUE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_tv), FALSE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(log_tv), 6);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(log_tv), 6);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(log_tv), 4);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(log_tv), 4);
        gtk_widget_set_vexpand(log_tv, TRUE);
        gtk_widget_set_focusable(log_tv, TRUE);
        self->log_text_view = log_tv;

        GtkWidget *log_scroll = gtk_scrolled_window_new();
        gtk_widget_add_css_class(log_scroll, "silktex-compile-log");
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(log_scroll), 150);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), log_tv);
        gtk_widget_set_visible(log_scroll, FALSE);
        self->log_scroll = log_scroll;

        g_signal_connect(log_scroll, "notify::visible", G_CALLBACK(on_log_scroll_visible_changed),
                         self);

        if (self->log_paned) {
            gtk_paned_set_end_child(self->log_paned, log_scroll);
        }

        if (self->btn_log) {
            g_object_bind_property(self->btn_log, "active", log_scroll, "visible",
                                   G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
        }
    }

    if (self->btn_tools_toggle) {
        g_signal_connect(self->btn_tools_toggle, "notify::active",
                         G_CALLBACK(on_tools_split_toggle_active), self);
        on_tools_split_toggle_active(self->btn_tools_toggle, NULL, self);
    }

    silktex_window_new_tab(self);
    silktex_window_git_refresh_state(self);

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
        {"win.editor-zoom-in", "<Control>plus"},
        {"win.editor-zoom-out", "<Control>minus"},
        {"win.editor-zoom-reset", "<Control><Shift>0"},
        {"win.zoom-fit", "<Control>0"},
        {"win.command-palette", "<Control><Shift>p"},
        {"win.find", "<Control><Shift>f"},
        {"win.find-replace", "<Control>f"},
        {"win.forward-sync", "<Control><Alt>f"},
        {"win.toggle-preview", "F9"},
        {"win.toggle-sidebar", "F8"},
        {"win.fullscreen", "F11"},
        {"win.open-menu", "F10"},
        {"win.preferences", "<Control>comma"},
        {"win.shortcuts", "<Control>question"},
        {"win.next-page", "Page_Down"},
        {"win.prev-page", "Page_Up"},
        {"app.quit", "<Control>q"},
    };

    GtkApplication *app = GTK_APPLICATION(g_application_get_default());
    for (size_t i = 0; i < G_N_ELEMENTS(accels); i++) {
        const char *accel_list[] = {accels[i][1], NULL};
        gtk_application_set_accels_for_action(app, accels[i][0], accel_list);
    }
    /* Also accept Ctrl+PgUp/PgDown for page navigation. */
    const char *next_page_accels[] = {"Page_Down", "<Control>Page_Down", NULL};
    const char *prev_page_accels[] = {"Page_Up",   "<Control>Page_Up",   NULL};
    gtk_application_set_accels_for_action(app, "win.next-page", next_page_accels);
    gtk_application_set_accels_for_action(app, "win.prev-page", prev_page_accels);
    /* Accept Ctrl+= as alias for Ctrl++ (= and + share the same key). */
    const char *zoom_in_accels[] = {"<Control>plus", "<Control>equal", NULL};
    gtk_application_set_accels_for_action(app, "win.editor-zoom-in", zoom_in_accels);

    silktex_window_update_page_label(self);
}

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

static char *find_default_template_path(void)
{
    char *path = g_build_filename(SILKTEX_DATA, "templates", "default.tex", NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
    g_free(path);

    const char *const *dirs = g_get_system_data_dirs();
    for (int i = 0; dirs && dirs[i]; i++) {
        path = g_build_filename(dirs[i], "silktex", "templates", "default.tex", NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
        g_free(path);
    }
    return NULL;
}

void silktex_window_new_tab(SilktexWindow *self)
{
    g_return_if_fail(SILKTEX_IS_WINDOW(self));

    SilktexEditor *editor = silktex_editor_new();

    g_autofree char *template_path = find_default_template_path();
    if (template_path && g_file_test(template_path, G_FILE_TEST_EXISTS)) {
        g_autofree char *text = NULL;
        if (g_file_get_contents(template_path, &text, NULL, NULL))
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
