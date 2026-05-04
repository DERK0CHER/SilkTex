/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * P2P real-time collaboration via Loro CRDT + libp2p.
 * The networking runs in silktex-node (a Rust sidecar subprocess).
 */

#pragma once

#include "editor.h"
#include "window.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Wire up the collaboration menu button in the header bar.
 * Called once from silktex_window_init after gtk_widget_init_template. */
void silktex_collab_setup_window   (SilktexWindow *window);

/* Connect an editor to the collaboration layer.
 * Hooks into the buffer's insert-text / delete-range signals.
 * Lazily spawns silktex-node on the first call.
 * Safe to call on every tab — only the first call starts the node.
 * No-op while a session is active or pending: the session stays bound to
 * the editor it was started on, so opening new tabs doesn't disrupt it. */
void silktex_collab_connect_editor (SilktexEditor *editor);

/* Leave any active session and tear down the node subprocess.
 * Called from SilktexWindow dispose so closing the window also leaves. */
void silktex_collab_shutdown (void);

/* TRUE if a session is active or pending and `editor` is the document
 * bound to it. Used by the window to refuse closing a session-bound tab. */
gboolean silktex_collab_is_bound_editor (SilktexEditor *editor);

/* Leave the active session (no-op if none). Exposed so the window's
 * "Leave Session" toast button can hang up without driving the popover. */
void silktex_collab_leave_session (void);

G_END_DECLS
