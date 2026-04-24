/*
 * SilkTex - Inline find/replace bar
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <gtk/gtk.h>
#include "silktex-editor.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_SEARCHBAR (silktex_searchbar_get_type())
G_DECLARE_FINAL_TYPE(SilktexSearchbar, silktex_searchbar, SILKTEX, SEARCHBAR, GtkWidget)

SilktexSearchbar *silktex_searchbar_new(void);

/* Show / hide the bar, optionally in replace mode */
void silktex_searchbar_open(SilktexSearchbar *self, gboolean replace_mode);
void silktex_searchbar_close(SilktexSearchbar *self);

/* Set editor that operations act on */
void silktex_searchbar_set_editor(SilktexSearchbar *self, SilktexEditor *editor);

G_END_DECLS
