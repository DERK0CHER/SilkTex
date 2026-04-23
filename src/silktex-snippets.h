/*
 * SilkTex - Snippet engine
 * SPDX-License-Identifier: MIT
 *
 * Supports the same snippet file format as the original Gummi:
 *
 *   snippet KEY[,ACCEL[,Name]]
 *   \tEXPANDED TEXT with $1, $2, ${3:default} placeholders
 *
 * Tab / Shift-Tab navigate between placeholders; Escape deactivates.
 */
#pragma once
#include <glib-object.h>
#include "silktex-editor.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_SNIPPETS (silktex_snippets_get_type())
G_DECLARE_FINAL_TYPE(SilktexSnippets, silktex_snippets, SILKTEX, SNIPPETS, GObject)

SilktexSnippets *silktex_snippets_new(void);

/* Load / save the snippets file */
void silktex_snippets_load(SilktexSnippets *self);
void silktex_snippets_save(SilktexSnippets *self);
void silktex_snippets_reset_to_default(SilktexSnippets *self);

/* Key-press handler – returns TRUE if the event was consumed */
gboolean silktex_snippets_handle_key(SilktexSnippets *self,
                                     SilktexEditor   *editor,
                                     guint            keyval,
                                     GdkModifierType  state);

G_END_DECLS
