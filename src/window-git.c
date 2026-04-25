/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Git integration for the main window — status dialog, stage/unstage, commit,
 * pull, and push. All subprocess / blocking work runs on worker threads via
 * GTask; callbacks hop back to the UI thread to update widgets and toasts.
 *
 * Low-level git operations live in git.c; this file only wires them to GTK
 * and AdwDialog. Window pointers in async code use GWeakRef (or are cleared
 * on dialog close) so callbacks never touch a destroyed window.
 */

#include "window-private.h"
#include "i18n.h"

/* -------------------------------------------------------------------------- */
/* Task payloads — freed via g_task_set_task_data destroy notify or GClosure */

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

/* -------------------------------------------------------------------------- */
/* Small helpers */

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

/*
 * Disable win.git-* actions when the git binary is missing so menu entries
 * do not advertise features that cannot run.
 */
void silktex_window_git_update_actions(SilktexWindow *self)
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

/* Runs on a worker thread — must not call GTK; only silktex_git_* and GTask APIs. */

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

static void update_git_dialog(SilktexWindow *self);

/* UI thread — resolves GTask, shows toast, refreshes status list */

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

    silktex_window_git_refresh_state(self);
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

/*
 * Drop stale results if the user switched tabs or paths before the worker
 * finished — compares requested path to the active editor's on-disk path.
 */

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

    silktex_window_git_update_actions(self);
    update_git_dialog(self);
    g_object_unref(self);
}

/* Entry point from window.c on tab change, save, autosave, etc. */

void silktex_window_git_refresh_state(SilktexWindow *self)
{
    SilktexEditor *editor = silktex_window_get_active_editor(self);
    const char *filename = editor ? silktex_editor_get_filename(editor) : NULL;

    if (!silktex_git_is_available() || filename == NULL || *filename == '\0') {
        g_clear_pointer(&self->git_status, silktex_git_status_free);
        g_clear_pointer(&self->git_status_message, g_free);
        self->git_status_message =
            g_strdup(!silktex_git_is_available() ? _("git is not installed")
                                                 : _("Save the active document before using Git"));
        silktex_window_git_update_actions(self);
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

/*
 * Clear dangling pointers — the dialog's children are destroyed; keeping
 * GtkLabel* / GtkListBox* would be use-after-free.
 */

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
    silktex_window_git_refresh_state(SILKTEX_WINDOW(user_data));
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

/*
 * Do not call adw_dialog_present twice on the same dialog — it can confuse
 * AdwDialogHost / AdwFloatingSheet ("Broken accounting of active state").
 * If already open, just refresh the list.
 */

static void show_git_dialog(SilktexWindow *self)
{
    if (self->git_dialog != NULL) {
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
    silktex_window_git_refresh_state(self);
}

/*
 * Focus the commit field only after the sheet is mapped — grabbing focus in
 * the same tick as adw_dialog_present contributes to broken active state.
 */

static void on_git_commit_entry_mapped(GtkWidget *entry, gpointer user_data)
{
    (void)user_data;
    g_signal_handlers_disconnect_by_func(entry, G_CALLBACK(on_git_commit_entry_mapped), NULL);
    gtk_widget_grab_focus(entry);
}

static void action_git_commit(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    show_git_dialog(self);
    if (self->git_commit_message == NULL) return;
    GtkWidget *entry = GTK_WIDGET(self->git_commit_message);
    if (gtk_widget_get_mapped(entry))
        gtk_widget_grab_focus(entry);
    else
        g_signal_connect(entry, "map", G_CALLBACK(on_git_commit_entry_mapped), NULL);
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

static const GActionEntry git_actions[] = {
    {"git-status", action_git_status},
    {"git-commit", action_git_commit},
    {"git-pull", action_git_pull},
    {"git-push", action_git_push},
};

void silktex_window_git_register_actions(SilktexWindow *self)
{
    g_action_map_add_action_entries(G_ACTION_MAP(self), git_actions, G_N_ELEMENTS(git_actions),
                                    self);
}
