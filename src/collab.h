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
 * Safe to call on every tab — only the first call starts the node. */
void silktex_collab_connect_editor (SilktexEditor *editor);

G_END_DECLS
