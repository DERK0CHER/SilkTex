/*
 * SilkTex — gettext shorthand
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Wraps GLib gettext macros so sources need not include libintl.h directly.
 * GETTEXT_PACKAGE must match meson project name (see meson.build).
 */
#pragma once

#include <glib.h>

/* Keep translation helpers available without requiring libintl headers. */
#ifndef _
#define _(String) g_dgettext(GETTEXT_PACKAGE, (String))
#endif

#ifndef N_
#define N_(String) (String)
#endif

#ifndef C_
#define C_(Context, String) g_dpgettext2(GETTEXT_PACKAGE, (Context), (String))
#endif
