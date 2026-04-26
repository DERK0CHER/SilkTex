/*
 * SilkTex - SyncTeX forward/inverse sync helpers
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Invokes the `synctex` CLI (view / edit) instead of linking libsynctex so
 * minimal Flatpak SDKs can omit the library. Parses stdout for Page/Line/Column
 * and drives SilktexPreview scroll or SilktexEditor goto_line accordingly.
 */
#include "synctex.h"
#include "utils.h"
#include <glib.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ utils */

/* Run `synctex <args>` and return its stdout as a string, or NULL on error.
 * Caller must g_free() the result. */
static char *run_synctex(const char *args)
{
    g_autofree char *cmd = NULL;
    char *output = NULL;
    int status = 0;
    GError *err = NULL;

    if (g_getenv("FLATPAK_ID")) {
        g_autofree char *inner = g_strdup_printf("synctex %s", args);
        g_autofree char *quoted_inner = g_shell_quote(inner);
        cmd = g_strdup_printf("flatpak-spawn --host sh -c %s", quoted_inner);
    } else {
        cmd = g_strdup_printf("synctex %s", args);
    }

    g_spawn_command_line_sync(cmd, &output, NULL, &status, &err);
    if (err) {
        g_error_free(err);
        return NULL;
    }
    if (status != 0) {
        g_free(output);
        return NULL;
    }
    return output;
}

/* Parse a key: value pair from synctex output, e.g. "Page:3\n" → 3 */
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
    *out = strtod(p, NULL);
    return TRUE;
}

/* ---------------------------------------------------------------- forward */

gboolean silktex_synctex_forward(SilktexEditor *editor, SilktexPreview *preview,
                                 const char *pdf_path)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(preview), FALSE);
    g_return_val_if_fail(pdf_path != NULL, FALSE);

    const char *tex_path = silktex_editor_get_filename(editor);
    if (!tex_path) return FALSE;

    /* Get cursor line (1-based for synctex) */
    int line = silktex_editor_get_cursor_line(editor) + 1;

    g_autofree char *args =
        g_strdup_printf("view -i %d:0:\"%s\" -o \"%s\"", line, tex_path, pdf_path);

    g_autofree char *out = run_synctex(args);
    if (!out) {
        slog(L_INFO, "synctex forward: no output (synctex not installed or no .synctex.gz?)\n");
        return FALSE;
    }

    int page = 0;
    double x = 0.0, y = 0.0;
    parse_int_field(out, "Page:", &page);
    parse_double_field(out, "x:", &x);
    parse_double_field(out, "y:", &y);

    if (page < 1) return FALSE;

    /* Scroll preview to page (0-based) */
    silktex_preview_set_page(preview, page - 1);
    /* TODO: scroll to x/y within the page once the preview exposes such API */
    return TRUE;
}

/* --------------------------------------------------------------- inverse */

gboolean silktex_synctex_inverse(SilktexEditor *editor, const char *pdf_path, int page, double x,
                                 double y)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(editor), FALSE);
    g_return_val_if_fail(pdf_path != NULL, FALSE);

    /* synctex edit -o "page:x:y:pdf" */
    g_autofree char *args = g_strdup_printf("edit -o \"%d:%g:%g:%s\"", page + 1, x, y, pdf_path);

    g_autofree char *out = run_synctex(args);
    if (!out) {
        slog(L_INFO, "synctex inverse: no output\n");
        return FALSE;
    }

    /* Parse "Line:" field */
    int line = 0;
    if (!parse_int_field(out, "Line:", &line) || line < 1) return FALSE;

    silktex_editor_goto_line(editor, line - 1);
    return TRUE;
}
