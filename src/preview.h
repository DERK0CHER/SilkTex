/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PDF preview widget API: load path, page navigation, zoom, layout mode,
 * scroll-to coordinates for SyncTeX forward sync.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SILKTEX_TYPE_PREVIEW (silktex_preview_get_type())
G_DECLARE_FINAL_TYPE(SilktexPreview, silktex_preview, SILKTEX, PREVIEW, GtkWidget)

typedef enum {
    SILKTEX_PREVIEW_LAYOUT_CONTINUOUS,
    SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE,
} SilktexPreviewLayout;

SilktexPreview *silktex_preview_new(void);

gboolean silktex_preview_load_file(SilktexPreview *self, const char *path);
void silktex_preview_refresh(SilktexPreview *self);
void silktex_preview_clear(SilktexPreview *self);

int silktex_preview_get_page(SilktexPreview *self);
void silktex_preview_set_page(SilktexPreview *self, int page);
int silktex_preview_get_n_pages(SilktexPreview *self);

void silktex_preview_next_page(SilktexPreview *self);
void silktex_preview_prev_page(SilktexPreview *self);

double silktex_preview_get_zoom(SilktexPreview *self);
void silktex_preview_set_zoom(SilktexPreview *self, double zoom);
void silktex_preview_zoom_in(SilktexPreview *self);
void silktex_preview_zoom_out(SilktexPreview *self);
void silktex_preview_zoom_fit_width(SilktexPreview *self);
void silktex_preview_zoom_fit_page(SilktexPreview *self);

SilktexPreviewLayout silktex_preview_get_layout(SilktexPreview *self);
void silktex_preview_set_layout(SilktexPreview *self, SilktexPreviewLayout layout);

void silktex_preview_scroll_to_position(SilktexPreview *self, double x, double y);
void silktex_preview_set_inverted(SilktexPreview *self, gboolean inverted);
gboolean silktex_preview_get_inverted(SilktexPreview *self);

G_END_DECLS
