/*
 * SilkTex - Preferences dialog
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AdwPreferencesDialog subclass: editor, compile, file, and snippet settings.
 * Values are written through configfile; SilktexPrefsApplyFunc notifies the
 * main window to refresh all editors and the compiler without restart.
 */
#pragma once
#include <adwaita.h>
#include "editor.h"
#include "snippets.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_PREFS (silktex_prefs_get_type())
G_DECLARE_FINAL_TYPE(SilktexPrefs, silktex_prefs, SILKTEX, PREFS, AdwPreferencesDialog)

/* Pass a callback so the prefs dialog can notify the window to
 * apply changes to every open editor immediately. */
typedef void (*SilktexPrefsApplyFunc)(gpointer user_data);

SilktexPrefs *silktex_prefs_new(void);
void silktex_prefs_set_apply_callback(SilktexPrefs *self, SilktexPrefsApplyFunc func,
                                      gpointer user_data);
void silktex_prefs_set_snippets(SilktexPrefs *self, SilktexSnippets *snippets);
void silktex_prefs_present(SilktexPrefs *self, GtkWindow *parent);

G_END_DECLS
