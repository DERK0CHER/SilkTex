/*
 * SilkTex - Snippet engine
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Supports a VS Code-style JSON snippet file:
 *
 *   {
 *     "Display Name": {
 *       "prefix": "trigger",
 *       "accelerator": "u",
 *       "body": ["line 1", "line 2"],
 *       "description": "Shown in preferences"
 *     }
 *   }
 *
 * Tab / Shift-Tab navigate between placeholders; Escape deactivates.
 */
#pragma once
#include <glib-object.h>
#include "editor.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_SNIPPETS (silktex_snippets_get_type())
G_DECLARE_FINAL_TYPE(SilktexSnippets, silktex_snippets, SILKTEX, SNIPPETS, GObject)

SilktexSnippets *silktex_snippets_new(void);

const char *silktex_snippets_get_filename(SilktexSnippets *self);
void silktex_snippets_reload(SilktexSnippets *self);
void silktex_snippets_reset_to_default(SilktexSnippets *self);

/*
 * Set the two global modifier keys that are combined with each snippet's
 * single-letter accelerator to form its full shortcut.  Pass e.g. "Shift"
 * and "Alt" (case-insensitive).  An empty / NULL string disables that slot.
 *
 * Rebuilds the internal accelerator table; call whenever the preference
 * changes or the snippets file is reloaded.
 */
void silktex_snippets_set_modifiers(SilktexSnippets *self, const char *modifier1,
                                    const char *modifier2);

/* Key-press handler – returns TRUE if the event was consumed */
gboolean silktex_snippets_handle_key(SilktexSnippets *self, SilktexEditor *editor, guint keyval,
                                     GdkModifierType state);

/* Key-release handler – syncs mirrored placeholder groups after character
 * insertion.  Always returns FALSE (never consumes the event). */
gboolean silktex_snippets_handle_key_release(SilktexSnippets *self, SilktexEditor *editor,
                                             guint keyval, GdkModifierType state);

G_END_DECLS
