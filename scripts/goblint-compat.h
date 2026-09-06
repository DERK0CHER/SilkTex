/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Force-included by the Goblint CI job (see .github/workflows/goblint.yml).
 *
 * That job defines __GI_SCANNER__ so Graphene selects its scalar code path:
 * CIL cannot parse the SSE intrinsics or the GCC vector_size types Graphene
 * otherwise exposes through the GTK headers (graphene-config.h gates the whole
 * GRAPHENE_USE_* selection on that macro).
 *
 * GLib gates its g_auto/g_autoptr/g_autofree cleanup macros on the very same
 * macro (glib/gmacros.h), so defining it project-wide makes every use of
 * g_autofree a compile error and the build fails before Goblint ever runs.
 *
 * Pull the GLib stack in first with __GI_SCANNER__ undefined, so those headers
 * install their real macro definitions, then restore the define for the
 * Graphene headers GTK includes afterwards. Include guards keep the second
 * pass over the GLib headers a no-op.
 */

#ifndef SILKTEX_GOBLINT_COMPAT_H
#define SILKTEX_GOBLINT_COMPAT_H

#ifdef __GI_SCANNER__

#undef __GI_SCANNER__

#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#define __GI_SCANNER__ 1

#endif /* __GI_SCANNER__ */

#endif /* SILKTEX_GOBLINT_COMPAT_H */
