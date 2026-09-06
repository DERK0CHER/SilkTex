/*
 * SilkTex - SyncTeX forward/inverse sync helpers
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "synctex.h"
#include "utils.h"
#include <glib.h>
#include <string.h>
#include <stdlib.h>

static char *run_synctex(const char *const *args)
{
    char *output = NULL;
    g_autofree char *stderr_buf = NULL;
    int status = 0;
    GError *err = NULL;

    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup("synctex"));
    for (guint i = 0; args && args[i]; i++) {
        g_ptr_array_add(argv, g_strdup(args[i]));
    }
    g_ptr_array_add(argv, NULL);

    gboolean spawned = g_spawn_sync(NULL, (gchar **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL,
                                    NULL, &output, &stderr_buf, &status, &err);
    g_ptr_array_unref(argv);

    if (!spawned) {
        if (err) {
            g_error_free(err);
        }
        g_free(output);
        return NULL;
    }
    if (err) {
        g_error_free(err);
        g_free(output);
        return NULL;
    }
    if (status != 0) {
        g_free(output);
        return NULL;
    }
    return output;
}

static char *resolve_synctex_dir(SilktexEditor *editor, const char *pdf_path)
{
    const char *workfile = silktex_editor_get_workfile(editor);
    if (workfile && *workfile) return g_path_get_dirname(workfile);
    if (pdf_path && *pdf_path) return g_path_get_dirname(pdf_path);
    return NULL;
}

static void add_unique_path(GPtrArray *paths, const char *path)
{
    if (!path || !*path) return;

    for (guint i = 0; i < paths->len; i++) {
        if (g_strcmp0(g_ptr_array_index(paths, i), path) == 0) return;
    }
    g_ptr_array_add(paths, g_strdup(path));
}

static char *run_synctex_view(int line, const char *tex_path, const char *pdf_path,
                              const char *synctex_dir)
{
    g_autofree char *input = g_strdup_printf("%d:0:%s", line, tex_path);

    if (synctex_dir && *synctex_dir) {
        const char *args[] = {"view", "-i", input, "-o", pdf_path, "-d", synctex_dir, NULL};
        return run_synctex(args);
    }

    const char *args[] = {"view", "-i", input, "-o", pdf_path, NULL};
    return run_synctex(args);
}

static char *run_synctex_edit(int page, double x, double y, const char *pdf_path,
                              const char *synctex_dir)
{
    /* Locale-independent: "%g" would emit a decimal comma under e.g. de_DE. */
    char xbuf[G_ASCII_DTOSTR_BUF_SIZE], ybuf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(xbuf, sizeof xbuf, "%g", x);
    g_ascii_formatd(ybuf, sizeof ybuf, "%g", y);
    g_autofree char *output = g_strdup_printf("%d:%s:%s:%s", page + 1, xbuf, ybuf, pdf_path);

    if (synctex_dir && *synctex_dir) {
        const char *args[] = {"edit", "-o", output, "-d", synctex_dir, NULL};
        return run_synctex(args);
    }

    const char *args[] = {"edit", "-o", output, NULL};
    return run_synctex(args);
}

static gboolean parse_int_field(const char *output, const char *key, int *out)
{
    const char *p = strstr(output, key);
    if (!p) return FALSE;
    p += strlen(key);
    *out = (int)strtol(p, NULL, 10);
    return TRUE;
}
static gboolean parse_double_field(const char *output, const char *key, double *out)
{
    const char *p = strstr(output, key);
    if (!p) return FALSE;
    p += strlen(key);
    *out = g_ascii_strtod(p, NULL);
    return TRUE;
}

gboolean silktex_synctex_forward(SilktexEditor *editor, SilktexPreview *preview,
                                 const char *pdf_path)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(preview), FALSE);
    g_return_val_if_fail(pdf_path != NULL, FALSE);

    int line = silktex_editor_get_cursor_line(editor) + 1;

    g_autofree char *synctex_dir = resolve_synctex_dir(editor, pdf_path);
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

    /*
     * The compiler normally runs TeX on the cache workfile, but different
     * TeX/synctex versions can record either canonical, basename, or original
     * source paths. Try the common forms before reporting a missing mapping.
     */
    const char *workfile = silktex_editor_get_workfile(editor);
    const char *filename = silktex_editor_get_filename(editor);
    add_unique_path(paths, workfile);
    if (workfile) {
        g_autofree char *canonical = g_canonicalize_filename(workfile, NULL);
        g_autofree char *basename = g_path_get_basename(workfile);
        add_unique_path(paths, canonical);
        add_unique_path(paths, basename);
    }
    add_unique_path(paths, filename);
    if (filename) {
        g_autofree char *canonical = g_canonicalize_filename(filename, NULL);
        g_autofree char *basename = g_path_get_basename(filename);
        add_unique_path(paths, canonical);
        add_unique_path(paths, basename);
    }

    int page = 0;
    double x = 0.0, y = 0.0;
    g_autofree char *out = NULL;

    for (guint i = 0; i < paths->len && page < 1; i++) {
        const char *tex_path = g_ptr_array_index(paths, i);
        out = run_synctex_view(line, tex_path, pdf_path, synctex_dir);
        if (!out && synctex_dir) out = run_synctex_view(line, tex_path, pdf_path, NULL);
        if (!out) continue;

        /* Anchor keys at line start so paths echoed in "Output:" cannot match. */
        parse_int_field(out, "\nPage:", &page);
        parse_double_field(out, "\nx:", &x);
        parse_double_field(out, "\ny:", &y);
        if (page < 1) g_clear_pointer(&out, g_free);
    }
    g_ptr_array_unref(paths);

    if (page < 1) {
        slog(L_INFO, "synctex forward: no mapping for current line\n");
        return FALSE;
    }

    silktex_preview_set_page(preview, page - 1);
    silktex_preview_scroll_to_position(preview, x, y);
    return TRUE;
}

gboolean silktex_synctex_inverse(SilktexEditor *editor, const char *pdf_path, int page, double x,
                                 double y)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);
    g_return_val_if_fail(pdf_path != NULL, FALSE);

    g_autofree char *synctex_dir = resolve_synctex_dir(editor, pdf_path);
    g_autofree char *out = run_synctex_edit(page, x, y, pdf_path, synctex_dir);
    if (!out && synctex_dir) out = run_synctex_edit(page, x, y, pdf_path, NULL);
    if (!out) {
        slog(L_INFO, "synctex inverse: no output\n");
        return FALSE;
    }

    int line = 0;
    if (!parse_int_field(out, "\nLine:", &line) || line < 1) return FALSE;

    silktex_editor_goto_line(editor, line - 1);
    return TRUE;
}
