/*
 * SilkTex — GtkSourceView style scheme setup
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Prepends search paths for bundled schemes (Gruvbox, Lights out) and user
 * ~/.config/silktex/styles. silktex_resolved_style_scheme_id() maps config
 * "Editor"/"style_scheme" plus Adwaita dark/light to a concrete scheme id.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

void        silktex_init_style_scheme_paths(void);
const char *silktex_resolved_style_scheme_id(void);

G_END_DECLS
