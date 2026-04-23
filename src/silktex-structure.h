/*
 * SilkTex - Document outline sidebar
 * SPDX-License-Identifier: MIT
 *
 * Parses the active editor's buffer for \part, \chapter, \section, \subsection,
 * \subsubsection and \paragraph commands and renders them as a scrollable tree
 * list.  Clicking an entry jumps the editor cursor to the corresponding line.
 */
#pragma once

#include <gtk/gtk.h>
#include "silktex-editor.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_STRUCTURE (silktex_structure_get_type())
G_DECLARE_FINAL_TYPE(SilktexStructure, silktex_structure, SILKTEX, STRUCTURE, GtkWidget)

SilktexStructure *silktex_structure_new(void);

/* Sets the editor the outline should reflect.  Automatically triggers a refresh
 * and subscribes to the editor's `changed` signal. */
void silktex_structure_set_editor(SilktexStructure *self, SilktexEditor *editor);

/* Rescan the editor buffer now. */
void silktex_structure_refresh(SilktexStructure *self);

G_END_DECLS
