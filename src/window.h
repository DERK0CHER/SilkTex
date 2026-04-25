/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Public API for the main window. Implementation is split across window.c,
 * window-git.c, and window-primary-menu.c; internal layout lives in
 * window-private.h (not included here).
 */

#pragma once

#include <adwaita.h>
#include "editor.h"
#include "compiler.h"
#include "preview.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_WINDOW (silktex_window_get_type())
G_DECLARE_FINAL_TYPE(SilktexWindow, silktex_window, SILKTEX, WINDOW, AdwApplicationWindow)

SilktexWindow *silktex_window_new(AdwApplication *app);

void silktex_window_open_file(SilktexWindow *self, GFile *file);
void silktex_window_new_tab(SilktexWindow *self);

SilktexEditor *silktex_window_get_active_editor(SilktexWindow *self);
SilktexCompiler *silktex_window_get_compiler(SilktexWindow *self);
SilktexPreview *silktex_window_get_preview(SilktexWindow *self);

void silktex_window_show_toast(SilktexWindow *self, const char *message);

G_END_DECLS
