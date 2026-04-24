/*
 * SilkTex - LaTeX insertion helpers
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dialogs and generators for inserting images, tables, matrices, bibliography
 * and common LaTeX environments / structural commands into the active editor.
 */
#pragma once

#include <adwaita.h>
#include "silktex-editor.h"

G_BEGIN_DECLS

/* ---------- text generators (caller frees) ------------------------------ */

char *silktex_latex_generate_table(int rows, int cols,
                                   int borders,    /* 0=none 1=outer 2=all */
                                   int alignment); /* 0=l 1=c 2=r */
char *silktex_latex_generate_matrix(int bracket, int rows, int cols);
char *silktex_latex_generate_image(const char *path, const char *caption,
                                   const char *label, double scale);

/* ---------- dialogs ----------------------------------------------------- */

/* Present a modal dialog and, on accept, insert the generated LaTeX at the
 * cursor position of `editor`. The dialog is transient for `parent`. */
void silktex_latex_insert_image_dialog (GtkWindow *parent, SilktexEditor *editor);
void silktex_latex_insert_table_dialog (GtkWindow *parent, SilktexEditor *editor);
void silktex_latex_insert_matrix_dialog(GtkWindow *parent, SilktexEditor *editor);
void silktex_latex_insert_biblio_dialog(GtkWindow *parent, SilktexEditor *editor);

/* ---------- quick inserts ---------------------------------------------- */

/* Insert the supplied string at the cursor, wrapping any current selection
 * between `before` and `after` when `after != NULL`. */
void silktex_latex_insert_at_cursor(SilktexEditor *editor,
                                    const char    *before,
                                    const char    *after);

/* Structural convenience wrappers */
void silktex_latex_insert_structure(SilktexEditor *editor, const char *command);
void silktex_latex_insert_environment(SilktexEditor *editor, const char *env);

G_END_DECLS
