/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#include "silktex-compiler.h"
#include "configfile.h"
#include <glib/gstdio.h>
#include <signal.h>

struct _SilktexCompiler {
    GObject parent_instance;

    char *typesetter;
    gboolean shell_escape;
    gboolean synctex;

    GThread *compile_thread;
    GMutex compile_mutex;
    GCond compile_cv;

    gboolean keep_running;
    gboolean paused;
    gboolean compile_requested;
    gboolean compiling;

    SilktexEditor *pending_editor;
    char *compile_log;
    int error_lines[BUFSIZ];

    GPid typesetter_pid;
};

G_DEFINE_FINAL_TYPE(SilktexCompiler, silktex_compiler, G_TYPE_OBJECT)

enum {
    SIGNAL_COMPILE_STARTED,
    SIGNAL_COMPILE_FINISHED,
    SIGNAL_COMPILE_ERROR,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static gboolean
emit_compile_finished(gpointer user_data)
{
    SilktexCompiler *self = SILKTEX_COMPILER(user_data);
    g_signal_emit(self, signals[SIGNAL_COMPILE_FINISHED], 0);
    return G_SOURCE_REMOVE;
}

static gboolean
emit_compile_error(gpointer user_data)
{
    SilktexCompiler *self = SILKTEX_COMPILER(user_data);
    g_signal_emit(self, signals[SIGNAL_COMPILE_ERROR], 0);
    return G_SOURCE_REMOVE;
}

static gboolean
run_typesetter(SilktexCompiler *self, const char *workfile, const char *workdir)
{
    g_autofree char *cmd = NULL;
    g_autofree char *stdout_buf = NULL;
    g_autofree char *stderr_buf = NULL;
    GError *error = NULL;
    int exit_status = 0;

    GString *args = g_string_new(self->typesetter);
    g_string_append(args, " -interaction=nonstopmode");

    if (self->shell_escape) {
        g_string_append(args, " -shell-escape");
    }
    if (self->synctex) {
        g_string_append(args, " -synctex=1");
    }

    g_string_append_printf(args, " -output-directory=\"%s\"", workdir);
    g_string_append_printf(args, " \"%s\"", workfile);

    cmd = g_string_free(args, FALSE);

    gboolean result = g_spawn_command_line_sync(cmd, &stdout_buf, &stderr_buf, &exit_status, &error);

    if (!result) {
        g_warning("Failed to run typesetter: %s", error->message);
        g_error_free(error);
        return FALSE;
    }

    g_mutex_lock(&self->compile_mutex);
    g_free(self->compile_log);
    self->compile_log = g_strdup(stdout_buf ? stdout_buf : stderr_buf);
    g_mutex_unlock(&self->compile_mutex);

    return exit_status == 0;
}

static gpointer
compile_thread_func(gpointer data)
{
    SilktexCompiler *self = SILKTEX_COMPILER(data);

    while (TRUE) {
        g_mutex_lock(&self->compile_mutex);

        while (!self->compile_requested && self->keep_running) {
            g_cond_wait(&self->compile_cv, &self->compile_mutex);
        }

        if (!self->keep_running) {
            g_mutex_unlock(&self->compile_mutex);
            break;
        }

        if (self->paused) {
            g_mutex_unlock(&self->compile_mutex);
            continue;
        }

        self->compile_requested = FALSE;
        self->compiling = TRUE;

        SilktexEditor *editor = self->pending_editor;
        if (editor != NULL) {
            g_object_ref(editor);
        }

        g_mutex_unlock(&self->compile_mutex);

        if (editor == NULL) {
            self->compiling = FALSE;
            continue;
        }

        silktex_editor_update_workfile(editor);
        const char *workfile = silktex_editor_get_workfile(editor);

        if (workfile != NULL) {
            g_autofree char *workdir = g_path_get_dirname(workfile);
            gboolean success = run_typesetter(self, workfile, workdir);

            if (success) {
                g_idle_add(emit_compile_finished, self);
            } else {
                g_idle_add(emit_compile_error, self);
            }
        }

        g_object_unref(editor);

        g_mutex_lock(&self->compile_mutex);
        self->compiling = FALSE;
        g_mutex_unlock(&self->compile_mutex);
    }

    return NULL;
}

static void
silktex_compiler_dispose(GObject *object)
{
    SilktexCompiler *self = SILKTEX_COMPILER(object);

    silktex_compiler_stop(self);

    g_clear_pointer(&self->typesetter, g_free);
    g_clear_pointer(&self->compile_log, g_free);
    g_clear_object(&self->pending_editor);

    G_OBJECT_CLASS(silktex_compiler_parent_class)->dispose(object);
}

static void
silktex_compiler_finalize(GObject *object)
{
    SilktexCompiler *self = SILKTEX_COMPILER(object);

    g_mutex_clear(&self->compile_mutex);
    g_cond_clear(&self->compile_cv);

    G_OBJECT_CLASS(silktex_compiler_parent_class)->finalize(object);
}

static void
silktex_compiler_class_init(SilktexCompilerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = silktex_compiler_dispose;
    object_class->finalize = silktex_compiler_finalize;

    signals[SIGNAL_COMPILE_STARTED] =
        g_signal_new("compile-started",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);

    signals[SIGNAL_COMPILE_FINISHED] =
        g_signal_new("compile-finished",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);

    signals[SIGNAL_COMPILE_ERROR] =
        g_signal_new("compile-error",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);
}

static void
silktex_compiler_init(SilktexCompiler *self)
{
    g_mutex_init(&self->compile_mutex);
    g_cond_init(&self->compile_cv);

    self->typesetter = g_strdup("pdflatex");
    self->shell_escape = TRUE;
    self->synctex = FALSE;
    self->keep_running = FALSE;
    self->paused = FALSE;
    self->compiling = FALSE;
    self->compile_requested = FALSE;
}

SilktexCompiler *
silktex_compiler_new(void)
{
    return g_object_new(SILKTEX_TYPE_COMPILER, NULL);
}

void
silktex_compiler_set_typesetter(SilktexCompiler *self, const char *typesetter)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    g_free(self->typesetter);
    self->typesetter = g_strdup(typesetter);
    g_mutex_unlock(&self->compile_mutex);
}

const char *
silktex_compiler_get_typesetter(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), NULL);
    return self->typesetter;
}

void
silktex_compiler_start(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    if (self->compile_thread != NULL) {
        g_mutex_unlock(&self->compile_mutex);
        return;
    }

    self->keep_running = TRUE;
    self->compile_thread = g_thread_new("silktex-compiler", compile_thread_func, self);
    g_mutex_unlock(&self->compile_mutex);
}

void
silktex_compiler_stop(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    if (self->compile_thread == NULL) {
        g_mutex_unlock(&self->compile_mutex);
        return;
    }

    self->keep_running = FALSE;
    g_cond_signal(&self->compile_cv);
    g_mutex_unlock(&self->compile_mutex);

    g_thread_join(self->compile_thread);

    g_mutex_lock(&self->compile_mutex);
    self->compile_thread = NULL;
    g_mutex_unlock(&self->compile_mutex);
}

void
silktex_compiler_pause(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    self->paused = TRUE;
    g_mutex_unlock(&self->compile_mutex);
}

void
silktex_compiler_resume(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    self->paused = FALSE;
    g_cond_signal(&self->compile_cv);
    g_mutex_unlock(&self->compile_mutex);
}

void
silktex_compiler_request_compile(SilktexCompiler *self, SilktexEditor *editor)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    g_return_if_fail(editor == NULL || SILKTEX_IS_EDITOR(editor));

    g_mutex_lock(&self->compile_mutex);
    g_set_object(&self->pending_editor, editor);
    self->compile_requested = TRUE;
    g_cond_signal(&self->compile_cv);
    g_mutex_unlock(&self->compile_mutex);
}

void
silktex_compiler_force_compile(SilktexCompiler *self, SilktexEditor *editor)
{
    silktex_compiler_request_compile(self, editor);
}

void
silktex_compiler_cancel(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    if (self->typesetter_pid > 0) {
        kill(self->typesetter_pid, SIGTERM);
    }
    g_mutex_unlock(&self->compile_mutex);
}

gboolean
silktex_compiler_is_running(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    return self->compile_thread != NULL;
}

gboolean
silktex_compiler_is_compiling(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    return self->compiling;
}

void
silktex_compiler_set_shell_escape(SilktexCompiler *self, gboolean enabled)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    self->shell_escape = enabled;
}

void
silktex_compiler_set_synctex(SilktexCompiler *self, gboolean enabled)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    self->synctex = enabled;
}

void
silktex_compiler_apply_config(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    const char *ts = config_get_string("Compile", "typesetter");
    if (ts && *ts)
        silktex_compiler_set_typesetter(self, ts);
    silktex_compiler_set_shell_escape(self, config_get_boolean("Compile", "shellescape"));
    silktex_compiler_set_synctex(self,   config_get_boolean("Compile", "synctex"));
}

const char *
silktex_compiler_get_log(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), NULL);
    return self->compile_log;
}

int *
silktex_compiler_get_error_lines(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), NULL);
    return self->error_lines;
}

gboolean
silktex_compiler_run_makeindex(SilktexCompiler *self, SilktexEditor *editor)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);

    const char *workfile = silktex_editor_get_workfile(editor);
    if (workfile == NULL) return FALSE;

    g_autofree char *basename = g_path_get_basename(workfile);
    g_autofree char *dirname = g_path_get_dirname(workfile);

    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';

    g_autofree char *idx_file = g_strdup_printf("%s/%s.idx", dirname, basename);
    g_autofree char *cmd = g_strdup_printf("makeindex \"%s\"", idx_file);

    int exit_status = 0;
    gboolean result = g_spawn_command_line_sync(cmd, NULL, NULL, &exit_status, NULL);

    return result && exit_status == 0;
}

gboolean
silktex_compiler_run_bibtex(SilktexCompiler *self, SilktexEditor *editor)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);

    const char *workfile = silktex_editor_get_workfile(editor);
    if (workfile == NULL) return FALSE;

    g_autofree char *basename = g_path_get_basename(workfile);
    g_autofree char *dirname = g_path_get_dirname(workfile);

    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';

    g_autofree char *aux_file = g_strdup_printf("%s/%s", dirname, basename);
    g_autofree char *cmd = g_strdup_printf("cd \"%s\" && bibtex \"%s\"", dirname, basename);

    int exit_status = 0;
    gboolean result = g_spawn_command_line_sync(cmd, NULL, NULL, &exit_status, NULL);

    return result && exit_status == 0;
}
