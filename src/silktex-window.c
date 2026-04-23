/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#include "silktex-window.h"
#include "silktex-prefs.h"
#include "silktex-searchbar.h"
#include "silktex-snippets.h"
#include "silktex-synctex.h"
#include "configfile.h"
#include "constants.h"
#include <glib/gi18n.h>

struct _SilktexWindow {
    AdwApplicationWindow parent_instance;

    AdwWindowTitle *window_title;
    AdwToastOverlay *toast_overlay;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    GtkPaned *editor_paned;
    GtkBox *preview_box;
    GtkToggleButton *btn_preview;
    GtkButton *btn_compile;

    /* editor pane container – holds the searchbar + tab view */
    GtkBox *editor_vbox;  /* created programmatically */

    SilktexPreview *preview;
    SilktexCompiler *compiler;
    SilktexSearchbar *searchbar;
    SilktexSnippets *snippets;
    SilktexPrefs *prefs;

    guint compile_timer_id;
    gboolean auto_compile;
};

G_DEFINE_FINAL_TYPE(SilktexWindow, silktex_window, ADW_TYPE_APPLICATION_WINDOW)

static SilktexEditor *
get_editor_for_page(AdwTabPage *page)
{
    if (page == NULL) return NULL;
    GtkWidget *child = adw_tab_page_get_child(page);
    if (child == NULL) return NULL;
    return g_object_get_data(G_OBJECT(child), "silktex-editor");
}

static void
update_window_title(SilktexWindow *self)
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

static void
update_tab_title(SilktexWindow *self, AdwTabPage *page, SilktexEditor *editor)
{
    g_autofree char *basename = silktex_editor_get_basename(editor);
    const char *modified = silktex_editor_get_modified(editor) ? "*" : "";
    g_autofree char *title = g_strdup_printf("%s%s", modified, basename);
    adw_tab_page_set_title(page, title);
}

static gboolean
on_compile_timer(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    self->compile_timer_id = 0;

    if (self->auto_compile) {
        SilktexEditor *editor = silktex_window_get_active_editor(self);
        if (editor != NULL) {
            silktex_compiler_request_compile(self->compiler, editor);
        }
    }

    return G_SOURCE_REMOVE;
}

static void
restart_compile_timer(SilktexWindow *self)
{
    if (self->compile_timer_id > 0) {
        g_source_remove(self->compile_timer_id);
    }
    self->compile_timer_id = g_timeout_add_seconds(1, on_compile_timer, self);
}

static void
on_editor_changed(SilktexEditor *editor, gpointer user_data)
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

static void
on_compile_finished(SilktexCompiler *compiler, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        const char *pdffile = silktex_editor_get_pdffile(editor);
        if (pdffile != NULL && g_file_test(pdffile, G_FILE_TEST_EXISTS)) {
            silktex_preview_load_file(self->preview, pdffile);
        }
    }
}

static void
on_compile_error(SilktexCompiler *compiler, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_window_show_toast(self, _("Compilation error - check build log"));
}

static GtkWidget *
create_editor_page(SilktexWindow *self, SilktexEditor *editor)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled),
                                   silktex_editor_get_view(editor));
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);

    g_object_set_data_full(G_OBJECT(scrolled), "silktex-editor",
                           g_object_ref(editor), g_object_unref);

    g_signal_connect(editor, "changed", G_CALLBACK(on_editor_changed), self);

    return scrolled;
}

static void
action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_window_new_tab(self);
}

static void
on_open_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    if (file == NULL) {
        if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
            g_warning("Failed to open file: %s", error->message);
        }
        g_clear_error(&error);
        return;
    }

    silktex_window_open_file(self, file);
    g_object_unref(file);
}

static void
action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
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

static void
on_save_response(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file == NULL) {
        if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED)) {
            g_warning("Failed to save file: %s", error->message);
        }
        g_clear_error(&error);
        return;
    }

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        GError *save_error = NULL;
        if (!silktex_editor_save_file(editor, file, &save_error)) {
            silktex_window_show_toast(self, save_error->message);
            g_error_free(save_error);
        } else {
            AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
            update_tab_title(self, page, editor);
            update_window_title(self);
            silktex_window_show_toast(self, _("File saved"));
        }
    }

    g_object_unref(file);
}

static void
action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);

    if (editor == NULL) return;

    const char *filename = silktex_editor_get_filename(editor);
    if (filename != NULL) {
        GFile *file = g_file_new_for_path(filename);
        GError *error = NULL;
        if (!silktex_editor_save_file(editor, file, &error)) {
            silktex_window_show_toast(self, error->message);
            g_error_free(error);
        } else {
            AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
            update_tab_title(self, page, editor);
            update_window_title(self);
        }
        g_object_unref(file);
    } else {
        GtkFileDialog *dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, _("Save LaTeX Document"));
        gtk_file_dialog_set_modal(dialog, TRUE);
        gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_save_response, self);
    }
}

static void
action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Save LaTeX Document As"));
    gtk_file_dialog_set_modal(dialog, TRUE);
    gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, on_save_response, self);
}

static void
action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);

    if (page != NULL) {
        SilktexEditor *editor = get_editor_for_page(page);
        if (editor != NULL && silktex_editor_get_modified(editor)) {
            silktex_window_show_toast(self, _("Document has unsaved changes"));
            return;
        }
        adw_tab_view_close_page(self->tab_view, page);
    }
}

static void
action_undo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        silktex_editor_undo(editor);
    }
}

static void
action_redo(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        silktex_editor_redo(editor);
    }
}

static void
action_compile(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        silktex_compiler_force_compile(self->compiler, editor);
    }
}

static void
action_bold(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor) silktex_editor_apply_textstyle(editor, "bold");
}

static void
action_italic(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor) silktex_editor_apply_textstyle(editor, "italic");
}

static void
action_underline(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor) silktex_editor_apply_textstyle(editor, "underline");
}

static void
action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_preview_zoom_in(self->preview);
}

static void
action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_preview_zoom_out(self->preview);
}

static void
action_zoom_fit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_preview_zoom_fit_width(self->preview);
}

static void
action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor)
        silktex_searchbar_set_editor(self->searchbar, editor);
    silktex_searchbar_open(self->searchbar, FALSE);
}

static void
action_find_replace(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor)
        silktex_searchbar_set_editor(self->searchbar, editor);
    silktex_searchbar_open(self->searchbar, TRUE);
}

static void
action_forward_sync(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return;
    const char *pdf = silktex_editor_get_pdffile(editor);
    if (!pdf) return;
    if (!silktex_synctex_forward(editor, self->preview, pdf))
        silktex_window_show_toast(self, _("SyncTeX: synctex not available or no .synctex.gz"));
}

static void
action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    silktex_prefs_present(self->prefs, GTK_WINDOW(self));
}

static void
on_prefs_apply(gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    /* Apply settings to every open editor */
    guint n = adw_tab_view_get_n_pages(self->tab_view);
    for (guint i = 0; i < n; i++) {
        AdwTabPage *page = adw_tab_view_get_nth_page(self->tab_view, i);
        SilktexEditor *editor = get_editor_for_page(page);
        if (editor)
            silktex_editor_apply_settings(editor);
    }
    config_save();
    self->auto_compile = config_get_boolean("Compile", "auto_compile");
}

static void
on_preview_toggled(GtkToggleButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    gboolean visible = gtk_toggle_button_get_active(button);
    gtk_widget_set_visible(GTK_WIDGET(self->preview_box), visible);
}

static void
on_tab_changed(AdwTabView *view, GParamSpec *pspec, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    update_window_title(self);

    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (editor != NULL) {
        silktex_searchbar_set_editor(self->searchbar, editor);
        if (self->auto_compile)
            restart_compile_timer(self);
    }
}

static gboolean
on_close_page(AdwTabView *view, AdwTabPage *page, gpointer user_data)
{
    SilktexEditor *editor = get_editor_for_page(page);
    if (editor != NULL && silktex_editor_get_modified(editor)) {
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static const GActionEntry win_actions[] = {
    { "new", action_new },
    { "open", action_open },
    { "save", action_save },
    { "save-as", action_save_as },
    { "close-tab", action_close_tab },
    { "undo", action_undo },
    { "redo", action_redo },
    { "compile", action_compile },
    { "bold", action_bold },
    { "italic", action_italic },
    { "underline", action_underline },
    { "zoom-in", action_zoom_in },
    { "zoom-out", action_zoom_out },
    { "zoom-fit", action_zoom_fit },
    { "find", action_find },
    { "find-replace", action_find_replace },
    { "forward-sync", action_forward_sync },
    { "preferences", action_preferences },
};

/* Route key events to the snippet engine on the active editor */
static gboolean
on_window_key_pressed(GtkEventControllerKey *ctrl,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      gpointer               user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    if (!editor) return GDK_EVENT_PROPAGATE;
    return silktex_snippets_handle_key(self->snippets, editor, keyval, state);
}

static void
silktex_window_dispose(GObject *object)
{
    SilktexWindow *self = SILKTEX_WINDOW(object);

    if (self->compile_timer_id > 0) {
        g_source_remove(self->compile_timer_id);
        self->compile_timer_id = 0;
    }

    silktex_compiler_stop(self->compiler);
    g_clear_object(&self->compiler);
    g_clear_object(&self->snippets);
    g_clear_object(&self->prefs);

    G_OBJECT_CLASS(silktex_window_parent_class)->dispose(object);
}

static void
silktex_window_class_init(SilktexWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = silktex_window_dispose;

    gtk_widget_class_set_template_from_resource(widget_class, "/app/silktex/main.ui");

    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, window_title);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, toast_overlay);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, tab_view);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, tab_bar);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, editor_paned);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, preview_box);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_preview);
    gtk_widget_class_bind_template_child(widget_class, SilktexWindow, btn_compile);
}

static void
silktex_window_init(SilktexWindow *self)
{
    g_type_ensure(SILKTEX_TYPE_PREVIEW);
    gtk_widget_init_template(GTK_WIDGET(self));

    g_action_map_add_action_entries(G_ACTION_MAP(self), win_actions,
                                    G_N_ELEMENTS(win_actions), self);

    self->compiler = silktex_compiler_new();
    silktex_compiler_start(self->compiler);

    g_signal_connect(self->compiler, "compile-finished",
                     G_CALLBACK(on_compile_finished), self);
    g_signal_connect(self->compiler, "compile-error",
                     G_CALLBACK(on_compile_error), self);

    self->preview = silktex_preview_new();
    gtk_box_append(self->preview_box, GTK_WIDGET(self->preview));

    /* Search bar – inserted above the tab bar inside the paned left child */
    self->searchbar = silktex_searchbar_new();
    GtkWidget *paned_child = gtk_paned_get_start_child(self->editor_paned);
    if (GTK_IS_BOX(paned_child)) {
        gtk_box_prepend(GTK_BOX(paned_child), GTK_WIDGET(self->searchbar));
    }

    /* Snippets engine */
    self->snippets = silktex_snippets_new();
    GtkEventControllerKey *key_ctrl = GTK_EVENT_CONTROLLER_KEY(
        gtk_event_controller_key_new());
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key_ctrl),
                                               GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(key_ctrl));
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_window_key_pressed), self);

    /* Preferences dialog */
    self->prefs = silktex_prefs_new();
    silktex_prefs_set_apply_callback(self->prefs, on_prefs_apply, self);

    g_signal_connect(self->btn_preview, "toggled",
                     G_CALLBACK(on_preview_toggled), self);
    g_signal_connect(self->tab_view, "notify::selected-page",
                     G_CALLBACK(on_tab_changed), self);
    g_signal_connect(self->tab_view, "close-page",
                     G_CALLBACK(on_close_page), self);

    self->auto_compile = config_get_boolean("Compile", "auto_compile");

    silktex_window_new_tab(self);

    gtk_paned_set_position(self->editor_paned, 600);

    const char *accels[][2] = {
        { "win.new", "<Control>n" },
        { "win.open", "<Control>o" },
        { "win.save", "<Control>s" },
        { "win.save-as", "<Control><Shift>s" },
        { "win.close-tab", "<Control>w" },
        { "win.undo", "<Control>z" },
        { "win.redo", "<Control><Shift>z" },
        { "win.compile", "<Control>Return" },
        { "win.bold", "<Control>b" },
        { "win.italic", "<Control>i" },
        { "win.underline", "<Control>u" },
        { "win.zoom-in", "<Control>plus" },
        { "win.zoom-out", "<Control>minus" },
        { "win.zoom-fit", "<Control>0" },
        { "win.find", "<Control>f" },
        { "win.find-replace", "<Control>h" },
        { "win.forward-sync", "<Control><Shift>f" },
        { "win.preferences", "<Control>comma" },
    };

    GtkApplication *app = GTK_APPLICATION(g_application_get_default());
    for (size_t i = 0; i < G_N_ELEMENTS(accels); i++) {
        const char *accel_list[] = { accels[i][1], NULL };
        gtk_application_set_accels_for_action(app, accels[i][0], accel_list);
    }
}

SilktexWindow *
silktex_window_new(AdwApplication *app)
{
    return g_object_new(SILKTEX_TYPE_WINDOW, "application", app, NULL);
}

void
silktex_window_open_file(SilktexWindow *self, GFile *file)
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

    if (self->auto_compile) {
        restart_compile_timer(self);
    }
}

void
silktex_window_new_tab(SilktexWindow *self)
{
    g_return_if_fail(SILKTEX_IS_WINDOW(self));

    SilktexEditor *editor = silktex_editor_new();
    GtkWidget *page_widget = create_editor_page(self, editor);
    AdwTabPage *page = adw_tab_view_append(self->tab_view, page_widget);

    adw_tab_page_set_title(page, _("Untitled"));
    adw_tab_view_set_selected_page(self->tab_view, page);

    g_object_unref(editor);

    update_window_title(self);
}

SilktexEditor *
silktex_window_get_active_editor(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);

    AdwTabPage *page = adw_tab_view_get_selected_page(self->tab_view);
    return get_editor_for_page(page);
}

SilktexCompiler *
silktex_window_get_compiler(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);
    return self->compiler;
}

SilktexPreview *
silktex_window_get_preview(SilktexWindow *self)
{
    g_return_val_if_fail(SILKTEX_IS_WINDOW(self), NULL);
    return self->preview;
}

void
silktex_window_show_toast(SilktexWindow *self, const char *message)
{
    g_return_if_fail(SILKTEX_IS_WINDOW(self));

    AdwToast *toast = adw_toast_new(message);
    adw_toast_overlay_add_toast(self->toast_overlay, toast);
}
