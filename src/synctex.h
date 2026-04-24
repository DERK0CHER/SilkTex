/*
 * SilkTex - SyncTeX forward/inverse sync helpers
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Forward sync: editor cursor position  → PDF view position
 * Inverse sync: click in PDF view       → editor cursor position
 */
#pragma once
#include <glib-object.h>
#include "editor.h"
#include "preview.h"

G_BEGIN_DECLS

/* Forward sync ------------------------------------------------------------ */

/**
 * silktex_synctex_forward:
 * @editor:   the active editor
 * @preview:  the preview widget to scroll
 * @pdf_path: path to the compiled PDF file
 *
 * Reads the cursor line from @editor, calls synctex_parser to find the
 * corresponding page/x/y in @pdf_path and scrolls @preview to that location.
 *
 * Returns: TRUE on success.
 */
gboolean silktex_synctex_forward(SilktexEditor *editor, SilktexPreview *preview,
                                 const char *pdf_path);

/* Inverse sync ------------------------------------------------------------ */

/**
 * silktex_synctex_inverse:
 * @editor:   the editor to place the cursor in
 * @pdf_path: path to the compiled PDF
 * @page:     0-based page index in the PDF
 * @x:        PDF-space x coordinate (Poppler points)
 * @y:        PDF-space y coordinate (Poppler points)
 *
 * Uses synctex_display_query to find the source line in the .tex file and
 * places the cursor there.
 *
 * Returns: TRUE on success.
 */
gboolean silktex_synctex_inverse(SilktexEditor *editor, const char *pdf_path, int page, double x,
                                 double y);

G_END_DECLS
