/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "compiler.h"
#include "configfile.h"
#include "i18n.h"
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

    GSubprocess *child;
};

G_DEFINE_FINAL_TYPE (SilktexCompiler, silktex_compiler, G_TYPE_OBJECT)

    enum { SIGNAL_COMPILE_STARTED, SIGNAL_COMPILE_FINISHED, SIGNAL_COMPILE_ERROR, N_SIGNALS };

static guint signals[N_SIGNALS];

static gboolean running_in_flatpak(void)
{
    return g_getenv("FLATPAK_ID") != NULL;
}

/* @self: worker-thread caller whose child may be cancelled via
 * silktex_compiler_cancel()/stop(); NULL for synchronous main-thread runs. */
static gboolean spawn_tex_command(SilktexCompiler *self, const char *working_dir, GPtrArray *argv,
                                  char **stdout_buf, char **stderr_buf, int *exit_status,
                                  GError **error)
{
    const char *program = (argv && argv->len > 0) ? g_ptr_array_index(argv, 0) : NULL;
    const char *cwd = working_dir && *working_dir ? working_dir : NULL;

    /* TeX tools are resolved from PATH only; inside Flatpak that is the
     * org.freedesktop.Sdk.Extension.texlive extension mounted at /app/texlive.
     * Report a missing tool ourselves so the log says how to fix it. A program
     * given as a path is left to the spawn, which resolves it against cwd. */
    if (program != NULL && strchr(program, G_DIR_SEPARATOR) == NULL) {
        g_autofree char *found = g_find_program_in_path(program);
        if (found == NULL) {
            if (running_in_flatpak()) {
                g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT,
                            _("%s not found. Install the TeX Live extension: flatpak install "
                              "flathub org.freedesktop.Sdk.Extension.texlive//25.08"),
                            program);
            } else {
                g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT,
                            _("%s not found. Install a TeX distribution (TeX Live or MiKTeX) and "
                              "make sure it is on PATH."),
                            program);
            }
            return FALSE;
        }
    }

    GSubprocessFlags flags = G_SUBPROCESS_FLAGS_NONE;
    if (stdout_buf) flags |= G_SUBPROCESS_FLAGS_STDOUT_PIPE;
    if (stderr_buf) flags |= G_SUBPROCESS_FLAGS_STDERR_PIPE;
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(flags);
    if (cwd) g_subprocess_launcher_set_cwd(launcher, cwd);

    g_autoptr(GSubprocess) proc =
        g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv->pdata, error);
    if (proc == NULL) return FALSE;

    if (self) {
        g_mutex_lock(&self->compile_mutex);
        self->child = g_object_ref(proc);
        g_mutex_unlock(&self->compile_mutex);
    }

    g_autoptr(GBytes) out_bytes = NULL;
    g_autoptr(GBytes) err_bytes = NULL;
    gboolean ok = g_subprocess_communicate(proc, NULL, NULL, stdout_buf ? &out_bytes : NULL,
                                           stderr_buf ? &err_bytes : NULL, error);

    if (self) {
        g_mutex_lock(&self->compile_mutex);
        g_clear_object(&self->child);
        g_mutex_unlock(&self->compile_mutex);
    }

    if (!ok) return FALSE;

    if (stdout_buf) {
        gsize len = 0;
        const char *data = out_bytes ? g_bytes_get_data(out_bytes, &len) : NULL;
        *stdout_buf = g_strndup(data ? data : "", len);
    }
    if (stderr_buf) {
        gsize len = 0;
        const char *data = err_bytes ? g_bytes_get_data(err_bytes, &len) : NULL;
        *stderr_buf = g_strndup(data ? data : "", len);
    }
    if (exit_status) *exit_status = g_subprocess_get_status(proc);
    return TRUE;
}

typedef struct {
    SilktexCompiler *self;
    char *log;
    gboolean success;
} CompileResult;

static void compile_result_free(gpointer data)
{
    CompileResult *res = data;
    g_object_unref(res->self);
    g_free(res->log);
    g_free(res);
}

/* Runs on the main thread. compile_log is only ever written here (and in
 * dispose), so silktex_compiler_get_log() callers never race the worker. */
static gboolean emit_compile_result(gpointer user_data)
{
    CompileResult *res = user_data;
    SilktexCompiler *self = res->self;

    if (res->log != NULL) {
        g_free(self->compile_log);
        self->compile_log = g_steal_pointer(&res->log);
    }
    g_signal_emit(self, signals[res->success ? SIGNAL_COMPILE_FINISHED : SIGNAL_COMPILE_ERROR], 0);
    return G_SOURCE_REMOVE;
}

/* Back up PDF/synctex before invoking the typesetter; restore on failure so
 * the preview always shows the last successfully rendered version. */
static gboolean run_typesetter(SilktexCompiler *self, const char *workfile, const char *outdir,
                               const char *source_dir, char **log_out)
{
    g_autofree char *stdout_buf = NULL;
    g_autofree char *stderr_buf = NULL;
    g_autofree char *typesetter = NULL;
    gboolean shell_escape, synctex;
    GError *error = NULL;
    int exit_status = 0;

    /* Snapshot settings under the lock: apply_config() may replace them mid-run. */
    g_mutex_lock(&self->compile_mutex);
    typesetter = g_strdup(self->typesetter);
    shell_escape = self->shell_escape;
    synctex = self->synctex;
    g_mutex_unlock(&self->compile_mutex);

    g_autofree char *basename = g_path_get_basename(workfile);
    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';

    g_autofree char *final_pdf = g_strdup_printf("%s/%s.pdf", outdir, basename);
    g_autofree char *backup_pdf = g_strdup_printf("%s.lastgood", final_pdf);
    g_autofree char *final_synctex_gz = g_strdup_printf("%s/%s.synctex.gz", outdir, basename);
    g_autofree char *backup_synctex_gz = g_strdup_printf("%s.lastgood", final_synctex_gz);
    g_autofree char *final_synctex = g_strdup_printf("%s/%s.synctex", outdir, basename);
    g_autofree char *backup_synctex = g_strdup_printf("%s.lastgood", final_synctex);

    if (g_file_test(final_pdf, G_FILE_TEST_EXISTS)) {
        g_autoptr(GFile) src = g_file_new_for_path(final_pdf);
        g_autoptr(GFile) dst = g_file_new_for_path(backup_pdf);
        g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
    }
    if (g_file_test(final_synctex_gz, G_FILE_TEST_EXISTS)) {
        g_autoptr(GFile) src = g_file_new_for_path(final_synctex_gz);
        g_autoptr(GFile) dst = g_file_new_for_path(backup_synctex_gz);
        g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
    }
    if (g_file_test(final_synctex, G_FILE_TEST_EXISTS)) {
        g_autoptr(GFile) src = g_file_new_for_path(final_synctex);
        g_autoptr(GFile) dst = g_file_new_for_path(backup_synctex);
        g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
    }

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup(typesetter));
    g_ptr_array_add(argv, g_strdup("-interaction=nonstopmode"));
    g_ptr_array_add(argv, g_strdup("-halt-on-error"));
    g_ptr_array_add(argv, g_strdup("-file-line-error"));
    if (shell_escape) g_ptr_array_add(argv, g_strdup("-shell-escape"));
    if (synctex) g_ptr_array_add(argv, g_strdup("-synctex=1"));
    g_ptr_array_add(argv, g_strdup_printf("-output-directory=%s", outdir));
    g_ptr_array_add(argv, g_strdup_printf("-jobname=%s", basename));
    g_ptr_array_add(argv, g_strdup(workfile));
    g_ptr_array_add(argv, NULL);

    gboolean result =
        spawn_tex_command(self, source_dir, argv, &stdout_buf, &stderr_buf, &exit_status, &error);

    g_ptr_array_unref(argv);

    if (!result) {
        g_warning("Failed to run typesetter: %s", error ? error->message : "unknown");
        *log_out =
            g_strdup_printf("Failed to run typesetter: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        if (g_file_test(backup_pdf, G_FILE_TEST_EXISTS)) {
            g_autoptr(GFile) src = g_file_new_for_path(backup_pdf);
            g_autoptr(GFile) dst = g_file_new_for_path(final_pdf);
            g_file_move(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        }
        if (g_file_test(backup_synctex_gz, G_FILE_TEST_EXISTS)) {
            g_autoptr(GFile) src = g_file_new_for_path(backup_synctex_gz);
            g_autoptr(GFile) dst = g_file_new_for_path(final_synctex_gz);
            g_file_move(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        }
        if (g_file_test(backup_synctex, G_FILE_TEST_EXISTS)) {
            g_autoptr(GFile) src = g_file_new_for_path(backup_synctex);
            g_autoptr(GFile) dst = g_file_new_for_path(final_synctex);
            g_file_move(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        }
        return FALSE;
    }

    /* TeX logs may contain raw 8-bit bytes; GtkTextBuffer requires valid UTF-8. */
    const char *primary = (stdout_buf && *stdout_buf) ? stdout_buf : stderr_buf;
    *log_out = g_utf8_make_valid(primary ? primary : "", -1);

    gboolean success = (exit_status == 0);

    if (success) {
        g_remove(backup_pdf);
        g_remove(backup_synctex_gz);
        g_remove(backup_synctex);
    } else {
        /* Failed run may have truncated artifacts — restore last-good so
         * preview and SyncTeX mappings stay in sync. */
        if (g_file_test(backup_pdf, G_FILE_TEST_EXISTS)) {
            g_autoptr(GFile) src = g_file_new_for_path(backup_pdf);
            g_autoptr(GFile) dst = g_file_new_for_path(final_pdf);
            g_file_move(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        }
        if (g_file_test(backup_synctex_gz, G_FILE_TEST_EXISTS)) {
            g_autoptr(GFile) ssrc = g_file_new_for_path(backup_synctex_gz);
            g_autoptr(GFile) sdst = g_file_new_for_path(final_synctex_gz);
            g_file_move(ssrc, sdst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        }
        if (g_file_test(backup_synctex, G_FILE_TEST_EXISTS)) {
            g_autoptr(GFile) ssrc = g_file_new_for_path(backup_synctex);
            g_autoptr(GFile) sdst = g_file_new_for_path(final_synctex);
            g_file_move(ssrc, sdst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        }
    }

    return success;
}

static gpointer compile_thread_func(gpointer data)
{
    SilktexCompiler *self = SILKTEX_COMPILER(data);

    while (TRUE) {
        g_mutex_lock(&self->compile_mutex);

        while ((!self->compile_requested || self->paused) && self->keep_running) {
            g_cond_wait(&self->compile_cv, &self->compile_mutex);
        }

        if (!self->keep_running) {
            g_mutex_unlock(&self->compile_mutex);
            break;
        }

        self->compile_requested = FALSE;
        self->compiling = TRUE;

        SilktexEditor *editor = self->pending_editor;
        if (editor != NULL) {
            g_object_ref(editor);
        }

        g_mutex_unlock(&self->compile_mutex);

        if (editor == NULL) {
            g_mutex_lock(&self->compile_mutex);
            self->compiling = FALSE;
            g_mutex_unlock(&self->compile_mutex);
            continue;
        }

        /* Workfile was snapshotted on the main thread — never touch GtkTextBuffer here. */
        const char *workfile = silktex_editor_get_workfile(editor);

        if (workfile != NULL) {
            g_autofree char *outdir = g_path_get_dirname(workfile);
            g_autofree char *source_dir = silktex_editor_get_source_dir(editor);
            CompileResult *res = g_new0(CompileResult, 1);
            res->self = g_object_ref(self); /* keep alive until the idle has run */
            res->success = run_typesetter(self, workfile, outdir, source_dir, &res->log);
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, emit_compile_result, res, compile_result_free);
        }

        g_object_unref(editor);

        g_mutex_lock(&self->compile_mutex);
        self->compiling = FALSE;
        g_mutex_unlock(&self->compile_mutex);
    }

    return NULL;
}

static void silktex_compiler_dispose(GObject *object)
{
    SilktexCompiler *self = SILKTEX_COMPILER(object);

    silktex_compiler_stop(self);

    g_clear_pointer(&self->typesetter, g_free);
    g_clear_pointer(&self->compile_log, g_free);
    g_clear_object(&self->pending_editor);

    G_OBJECT_CLASS(silktex_compiler_parent_class)->dispose(object);
}

static void silktex_compiler_finalize(GObject *object)
{
    SilktexCompiler *self = SILKTEX_COMPILER(object);

    g_mutex_clear(&self->compile_mutex);
    g_cond_clear(&self->compile_cv);

    G_OBJECT_CLASS(silktex_compiler_parent_class)->finalize(object);
}

static void silktex_compiler_class_init(SilktexCompilerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = silktex_compiler_dispose;
    object_class->finalize = silktex_compiler_finalize;

    signals[SIGNAL_COMPILE_STARTED] =
        g_signal_new("compile-started", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                     NULL, G_TYPE_NONE, 0);

    signals[SIGNAL_COMPILE_FINISHED] =
        g_signal_new("compile-finished", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                     NULL, G_TYPE_NONE, 0);

    signals[SIGNAL_COMPILE_ERROR] =
        g_signal_new("compile-error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                     NULL, G_TYPE_NONE, 0);
}

static void silktex_compiler_init(SilktexCompiler *self)
{
    g_mutex_init(&self->compile_mutex);
    g_cond_init(&self->compile_cv);

    self->typesetter = g_strdup("pdflatex");
    self->shell_escape = FALSE;
    self->synctex = TRUE;
    self->keep_running = FALSE;
    self->paused = FALSE;
    self->compiling = FALSE;
    self->compile_requested = FALSE;
}

SilktexCompiler *silktex_compiler_new(void)
{
    return g_object_new(SILKTEX_TYPE_COMPILER, NULL);
}

void silktex_compiler_set_typesetter(SilktexCompiler *self, const char *typesetter)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    g_free(self->typesetter);
    self->typesetter = g_strdup(typesetter);
    g_mutex_unlock(&self->compile_mutex);
}

const char *silktex_compiler_get_typesetter(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), NULL);
    return self->typesetter;
}

void silktex_compiler_start(SilktexCompiler *self)
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

void silktex_compiler_stop(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    if (self->compile_thread == NULL) {
        g_mutex_unlock(&self->compile_mutex);
        return;
    }

    self->keep_running = FALSE;
    if (self->child != NULL) g_subprocess_send_signal(self->child, SIGTERM);
    g_cond_signal(&self->compile_cv);
    g_mutex_unlock(&self->compile_mutex);

    g_thread_join(self->compile_thread);

    g_mutex_lock(&self->compile_mutex);
    self->compile_thread = NULL;
    g_mutex_unlock(&self->compile_mutex);
}

void silktex_compiler_pause(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    self->paused = TRUE;
    g_mutex_unlock(&self->compile_mutex);
}

void silktex_compiler_resume(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    self->paused = FALSE;
    g_cond_signal(&self->compile_cv);
    g_mutex_unlock(&self->compile_mutex);
}

void silktex_compiler_request_compile(SilktexCompiler *self, SilktexEditor *editor)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    g_return_if_fail(editor == NULL || SILKTEX_IS_EDITOR(editor));

    g_mutex_lock(&self->compile_mutex);
    g_set_object(&self->pending_editor, editor);
    self->compile_requested = TRUE;
    g_cond_signal(&self->compile_cv);
    g_mutex_unlock(&self->compile_mutex);
}

void silktex_compiler_force_compile(SilktexCompiler *self, SilktexEditor *editor)
{
    silktex_compiler_request_compile(self, editor);
}

void silktex_compiler_cancel(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));

    g_mutex_lock(&self->compile_mutex);
    if (self->child != NULL) g_subprocess_send_signal(self->child, SIGTERM);
    g_mutex_unlock(&self->compile_mutex);
}

gboolean silktex_compiler_is_running(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    return self->compile_thread != NULL;
}

gboolean silktex_compiler_is_compiling(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    return self->compiling;
}

void silktex_compiler_set_shell_escape(SilktexCompiler *self, gboolean enabled)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    self->shell_escape = enabled;
}

void silktex_compiler_set_synctex(SilktexCompiler *self, gboolean enabled)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    self->synctex = enabled;
}

void silktex_compiler_apply_config(SilktexCompiler *self)
{
    g_return_if_fail(SILKTEX_IS_COMPILER(self));
    const char *ts = config_get_string("Compile", "typesetter");
    if (ts && *ts) silktex_compiler_set_typesetter(self, ts);
    silktex_compiler_set_shell_escape(self, config_get_boolean("Compile", "shellescape"));
    silktex_compiler_set_synctex(self, config_get_boolean("Compile", "synctex"));
}

const char *silktex_compiler_get_log(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), NULL);
    return self->compile_log;
}

int *silktex_compiler_get_error_lines(SilktexCompiler *self)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), NULL);
    return self->error_lines;
}

gboolean silktex_compiler_run_makeindex(SilktexCompiler *self, SilktexEditor *editor)
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
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup("makeindex"));
    g_ptr_array_add(argv, g_strdup(idx_file));
    g_ptr_array_add(argv, NULL);

    int exit_status = 0;
    gboolean result = spawn_tex_command(NULL, dirname, argv, NULL, NULL, &exit_status, NULL);
    g_ptr_array_unref(argv);

    return result && exit_status == 0;
}

gboolean silktex_compiler_run_bibtex(SilktexCompiler *self, SilktexEditor *editor)
{
    g_return_val_if_fail(SILKTEX_IS_COMPILER(self), FALSE);
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);

    const char *workfile = silktex_editor_get_workfile(editor);
    if (workfile == NULL) return FALSE;

    g_autofree char *basename = g_path_get_basename(workfile);
    g_autofree char *dirname = g_path_get_dirname(workfile);

    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup("bibtex"));
    g_ptr_array_add(argv, g_strdup(basename));
    g_ptr_array_add(argv, NULL);

    int exit_status = 0;
    gboolean result = spawn_tex_command(NULL, dirname, argv, NULL, NULL, &exit_status, NULL);
    g_ptr_array_unref(argv);

    return result && exit_status == 0;
}
