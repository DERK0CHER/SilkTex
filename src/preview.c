/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "preview.h"
#include <adwaita.h>
#if defined(__has_include)
#if __has_include(<poppler.h>)
#include <poppler.h>
#elif __has_include(<poppler/glib/poppler.h>)
#include <poppler/glib/poppler.h>
#else
#error "Poppler GLib headers not found"
#endif
#else
#include <poppler.h>
#endif
#include <math.h>

#define PAGE_GAP_BETWEEN 16
#define PAGE_PADDING     8

struct _SilktexPreview {
    GtkWidget parent_instance;

    GtkWidget *scrolled_window;
    GtkWidget *drawing_area;

    PopplerDocument *document;
    char *pdf_path;

    int current_page;
    int n_pages;
    double zoom;
    SilktexPreviewZoomMode zoom_mode;

    double page_width;
    double page_height;

    cairo_surface_t *cached_surface;
    int cached_page;
    double cached_zoom;
    int cached_scale;
    GPtrArray *page_surfaces;
    double total_height;

    SilktexPreviewLayout layout;
    gboolean scrolling_programmatically;
    gboolean inverted;
    guint fit_tick_id;
    guint rerender_debounce_id;

    gboolean magnifier_active;
    double magnifier_x;
    double magnifier_y;
};

G_DEFINE_FINAL_TYPE (SilktexPreview, silktex_preview, GTK_TYPE_WIDGET)

    enum { PROP_0, PROP_PAGE, PROP_N_PAGES, PROP_ZOOM, PROP_LAYOUT, N_PROPS };
enum { SIGNAL_INVERSE_SYNC_REQUESTED, N_SIGNALS };

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void silktex_preview_invalidate_cache(SilktexPreview *self)
{
    g_clear_pointer(&self->cached_surface, cairo_surface_destroy);
    self->cached_page = -1;
    self->cached_zoom = 0;
    self->cached_scale = 0;
    if (self->page_surfaces != NULL) {
        g_ptr_array_set_size(self->page_surfaces, 0);
    }
    self->total_height = 0;
}

static void on_scale_factor_changed(SilktexPreview *self, GParamSpec *pspec, GObject *obj)
{
    (void)obj;
    (void)pspec;
    silktex_preview_invalidate_cache(self);
    gtk_widget_queue_draw(self->drawing_area);
}

static cairo_surface_t *render_single_page(SilktexPreview *self, int index, int scale,
                                           double *out_page_w, double *out_page_h)
{
    PopplerPage *page = poppler_document_get_page(self->document, index);
    if (page == NULL) return NULL;

    double page_w = 0;
    double page_h = 0;
    poppler_page_get_size(page, &page_w, &page_h);
    if (out_page_w) *out_page_w = page_w;
    if (out_page_h) *out_page_h = page_h;

    double effective_zoom = self->zoom * scale;
    int width = MAX((int)ceil(page_w * effective_zoom), 1);
    int height = MAX((int)ceil(page_h * effective_zoom), 1);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_surface_set_device_scale(surface, (double)scale, (double)scale);

    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_scale(cr, self->zoom, self->zoom);
    poppler_page_render(page, cr);
    cairo_destroy(cr);
    g_object_unref(page);

    return surface;
}

static int surface_logical_width(cairo_surface_t *s)
{
    if (!s) return 0;
    double sx = 1, sy = 1;
    cairo_surface_get_device_scale(s, &sx, &sy);
    return (int)(cairo_image_surface_get_width(s) / (sx > 0 ? sx : 1.0));
}

static int surface_logical_height(cairo_surface_t *s)
{
    if (!s) return 0;
    double sx = 1, sy = 1;
    cairo_surface_get_device_scale(s, &sx, &sy);
    return (int)(cairo_image_surface_get_height(s) / (sy > 0 ? sy : 1.0));
}

static void silktex_preview_render_pages(SilktexPreview *self)
{
    if (self->document == NULL) return;
    if (self->n_pages <= 0) return;

    const int page_gap = PAGE_GAP_BETWEEN;
    int scale = gtk_widget_get_scale_factor(self->drawing_area);
    if (scale < 1) scale = 1;

    if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
        if (self->cached_surface != NULL && self->cached_page == self->current_page &&
            self->cached_scale == scale) {
            return;
        }
        silktex_preview_invalidate_cache(self);
        double page_w = 0, page_h = 0;
        cairo_surface_t *surface =
            render_single_page(self, self->current_page, scale, &page_w, &page_h);
        if (surface == NULL) return;
        self->cached_surface = surface;
        self->cached_page = self->current_page;
        self->cached_zoom = self->zoom;
        self->cached_scale = scale;
        self->page_width = page_w;
        self->page_height = page_h;
        int lw = surface_logical_width(surface);
        int lh = surface_logical_height(surface);
        self->total_height = lh + 2 * PAGE_PADDING;
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self->drawing_area),
                                           MAX(lw + 2 * PAGE_PADDING, 1));
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self->drawing_area),
                                            MAX((int)self->total_height, 1));
        return;
    }

    if (self->page_surfaces != NULL && self->page_surfaces->len == (guint)self->n_pages &&
        self->cached_scale == scale) {
        return;
    }

    silktex_preview_invalidate_cache(self);
    self->cached_zoom = self->zoom;
    self->cached_scale = scale;
    self->page_width = 0;
    self->page_height = 0;
    self->total_height = 0;

    int max_width = 0;

    for (int i = 0; i < self->n_pages; i++) {
        double page_w = 0, page_h = 0;
        cairo_surface_t *surface = render_single_page(self, i, scale, &page_w, &page_h);
        if (surface == NULL) {
            g_ptr_array_add(self->page_surfaces, NULL);
            continue;
        }
        if (i == 0) {
            self->page_width = page_w;
            self->page_height = page_h;
        }
        int lw = surface_logical_width(surface);
        int lh = surface_logical_height(surface);
        max_width = MAX(max_width, lw);
        self->total_height += lh;
        g_ptr_array_add(self->page_surfaces, surface);
    }

    if (self->n_pages > 1) self->total_height += (self->n_pages - 1) * page_gap;
    self->total_height += 2 * PAGE_PADDING;

    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self->drawing_area),
                                       MAX(max_width + 2 * PAGE_PADDING, 1));
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self->drawing_area),
                                        MAX((int)self->total_height, 1));
}

static void preview_bg_color(SilktexPreview *self, double *r, double *g, double *b)
{
    static const char *candidates[] = {"view_bg_color", "theme_bg_color"};

    gboolean from_theme = FALSE;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    {
        GtkStyleContext *ctx = gtk_widget_get_style_context(self->drawing_area);
        GdkRGBA rgba;
        for (guint i = 0; i < G_N_ELEMENTS(candidates); i++) {
            if (gtk_style_context_lookup_color(ctx, candidates[i], &rgba)) {
                *r = rgba.red;
                *g = rgba.green;
                *b = rgba.blue;
                from_theme = TRUE;
                break;
            }
        }
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (from_theme) return;

    AdwStyleManager *sm = adw_style_manager_get_default();
    if (adw_style_manager_get_dark(sm)) {
        *r = 0.15;
        *g = 0.15;
        *b = 0.16;
    } else {
        *r = 0.90;
        *g = 0.90;
        *b = 0.91;
    }
}

static void draw_page_paper_shadow(cairo_t *cr, double x, double y, int lw, int lh)
{
    for (int i = 3; i >= 1; i--) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.08 * (double)(4 - i));
        cairo_rectangle(cr, x + (double)i, y + (double)i, (double)lw, (double)lh);
        cairo_fill(cr);
    }
}

static void draw_surface_with_optional_invert(cairo_t *cr, cairo_surface_t *surface, double x,
                                              double y, int lw, int lh, gboolean inverted)
{
    cairo_set_source_surface(cr, surface, x, y);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    if (!inverted) return;

    cairo_save(cr);
    cairo_rectangle(cr, x, y, lw, lh);
    cairo_clip(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    (void)area;
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);

    double bg_r, bg_g, bg_b;
    preview_bg_color(self, &bg_r, &bg_g, &bg_b);
    cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
    cairo_paint(cr);

    if (self->document == NULL) {
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 16);
        const char *text = "No PDF loaded";
        cairo_text_extents_t extents;
        cairo_text_extents(cr, text, &extents);
        cairo_move_to(cr, (width - extents.width) / 2, (height + extents.height) / 2);
        cairo_show_text(cr, text);
        return;
    }

    silktex_preview_render_pages(self);

    /* Scale existing surfaces to the current zoom while a re-render is pending.
     * This makes the PDF track pane resizes instantly instead of jumping. */
    double scale_ratio = (self->cached_zoom > 0.001) ? (self->zoom / self->cached_zoom) : 1.0;

    if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
        if (self->cached_surface == NULL) return;
        int lw = surface_logical_width(self->cached_surface);
        int lh = surface_logical_height(self->cached_surface);
        int dw = (int)round(lw * scale_ratio);
        int dh = (int)round(lh * scale_ratio);
        double x = (width - dw) / 2.0;
        if (x < 0) x = PAGE_PADDING;
        double y = MAX((double)PAGE_PADDING, (height - dh) / 2.0);

        draw_page_paper_shadow(cr, x, y, dw, dh);
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, scale_ratio, scale_ratio);
        draw_surface_with_optional_invert(cr, self->cached_surface, 0, 0, lw, lh, self->inverted);
        cairo_restore(cr);
        return;
    }

    double y = PAGE_PADDING;
    for (guint i = 0; i < self->page_surfaces->len; i++) {
        cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
        if (surface == NULL) continue;
        int lw = surface_logical_width(surface);
        int lh = surface_logical_height(surface);
        int dw = (int)round(lw * scale_ratio);
        int dh = (int)round(lh * scale_ratio);
        double x = (width - dw) / 2.0;
        if (x < 0) x = PAGE_PADDING;

        draw_page_paper_shadow(cr, x, y, dw, dh);
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, scale_ratio, scale_ratio);
        draw_surface_with_optional_invert(cr, surface, 0, 0, lw, lh, self->inverted);
        cairo_restore(cr);

        y += dh + PAGE_GAP_BETWEEN;
    }

    /* ── Magnifier overlay ────────────────────────────────────── */
    if (self->magnifier_active) {
        const double R   = 88.0;  /* radius in logical pixels */
        const double MAG = 3.5;   /* magnification factor     */
        double mx = self->magnifier_x;
        double my = self->magnifier_y;

        /* Drop shadow. */
        cairo_save(cr);
        cairo_arc(cr, mx + 4, my + 4, R, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.22);
        cairo_fill(cr);
        cairo_restore(cr);

        /* Clip to circle, fill background, then magnify. */
        cairo_save(cr);
        cairo_arc(cr, mx, my, R, 0, 2 * M_PI);
        cairo_clip(cr);

        cairo_set_source_rgb(cr, bg_r, bg_g, bg_b);
        cairo_paint(cr);

        /* Magnification: scale around cursor point. */
        cairo_translate(cr, mx * (1.0 - MAG), my * (1.0 - MAG));
        cairo_scale(cr, MAG, MAG);

        /* Redraw page surfaces at their original positions. */
        if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
            if (self->cached_surface != NULL) {
                int lw = surface_logical_width(self->cached_surface);
                int lh = surface_logical_height(self->cached_surface);
                int dw = (int)round(lw * scale_ratio);
                int dh = (int)round(lh * scale_ratio);
                double px = (width - dw) / 2.0;
                if (px < 0) px = PAGE_PADDING;
                double py = MAX((double)PAGE_PADDING, (height - dh) / 2.0);
                cairo_save(cr);
                cairo_translate(cr, px, py);
                cairo_scale(cr, scale_ratio, scale_ratio);
                draw_surface_with_optional_invert(cr, self->cached_surface, 0, 0, lw, lh,
                                                  self->inverted);
                cairo_restore(cr);
            }
        } else {
            double py = PAGE_PADDING;
            for (guint i = 0; i < self->page_surfaces->len; i++) {
                cairo_surface_t *mag_surface = g_ptr_array_index(self->page_surfaces, i);
                if (mag_surface == NULL) continue;
                int lw = surface_logical_width(mag_surface);
                int lh = surface_logical_height(mag_surface);
                int dw = (int)round(lw * scale_ratio);
                int dh = (int)round(lh * scale_ratio);
                double px = (width - dw) / 2.0;
                if (px < 0) px = PAGE_PADDING;
                cairo_save(cr);
                cairo_translate(cr, px, py);
                cairo_scale(cr, scale_ratio, scale_ratio);
                draw_surface_with_optional_invert(cr, mag_surface, 0, 0, lw, lh, self->inverted);
                cairo_restore(cr);
                py += dh + PAGE_GAP_BETWEEN;
            }
        }

        cairo_restore(cr);

        /* Border ring. */
        cairo_save(cr);
        cairo_arc(cr, mx, my, R, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
        cairo_set_line_width(cr, 2.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
}

static void silktex_preview_dispose(GObject *object)
{
    SilktexPreview *self = SILKTEX_PREVIEW(object);
    if (self->rerender_debounce_id) {
        g_source_remove(self->rerender_debounce_id);
        self->rerender_debounce_id = 0;
    }
    silktex_preview_invalidate_cache(self);
    g_clear_object(&self->document);
    g_clear_pointer(&self->pdf_path, g_free);
    g_clear_pointer(&self->page_surfaces, g_ptr_array_unref);
    gtk_widget_unparent(self->scrolled_window);
    G_OBJECT_CLASS(silktex_preview_parent_class)->dispose(object);
}

static void silktex_preview_get_property(GObject *object, guint prop_id, GValue *value,
                                         GParamSpec *pspec)
{
    SilktexPreview *self = SILKTEX_PREVIEW(object);
    switch (prop_id) {
    case PROP_PAGE:
        g_value_set_int(value, self->current_page);
        break;
    case PROP_N_PAGES:
        g_value_set_int(value, self->n_pages);
        break;
    case PROP_ZOOM:
        g_value_set_double(value, self->zoom);
        break;
    case PROP_LAYOUT:
        g_value_set_int(value, (int)self->layout);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void silktex_preview_set_property(GObject *object, guint prop_id, const GValue *value,
                                         GParamSpec *pspec)
{
    SilktexPreview *self = SILKTEX_PREVIEW(object);
    switch (prop_id) {
    case PROP_PAGE:
        silktex_preview_set_page(self, g_value_get_int(value));
        break;
    case PROP_ZOOM:
        silktex_preview_set_zoom(self, g_value_get_double(value));
        break;
    case PROP_LAYOUT:
        silktex_preview_set_layout(self, (SilktexPreviewLayout)g_value_get_int(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void silktex_preview_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    SilktexPreview *self = SILKTEX_PREVIEW(widget);
    GTK_WIDGET_CLASS(silktex_preview_parent_class)->size_allocate(widget, width, height, baseline);

    if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_WIDTH) {
        silktex_preview_zoom_fit_width(self);
    } else if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_PAGE) {
        silktex_preview_zoom_fit_page(self);
    }
}

static void silktex_preview_class_init(SilktexPreviewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = silktex_preview_dispose;
    object_class->get_property = silktex_preview_get_property;
    object_class->set_property = silktex_preview_set_property;
    widget_class->size_allocate = silktex_preview_size_allocate;

    properties[PROP_PAGE] = g_param_spec_int("page", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READWRITE);
    properties[PROP_N_PAGES] =
        g_param_spec_int("n-pages", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE);
    properties[PROP_ZOOM] =
        g_param_spec_double("zoom", NULL, NULL, 0.1, 10.0, 1.0, G_PARAM_READWRITE);
    properties[PROP_LAYOUT] = g_param_spec_int("layout", NULL, NULL, 0, 1, 0, G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPS, properties);

    signals[SIGNAL_INVERSE_SYNC_REQUESTED] =
        g_signal_new("inverse-sync-requested", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
                     NULL, NULL, G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE);

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
}

/* Track current page by largest viewport overlap — more natural than centre-point. */
static void on_vadj_value_changed(GtkAdjustment *adj, gpointer user_data)
{
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    if (self->scrolling_programmatically) return;
    if (self->layout != SILKTEX_PREVIEW_LAYOUT_CONTINUOUS) return;
    if (self->document == NULL || self->page_surfaces == NULL) return;
    if (self->page_surfaces->len == 0) return;

    double scroll_top = gtk_adjustment_get_value(adj);
    double viewport_size = gtk_adjustment_get_page_size(adj);
    double scroll_bottom = scroll_top + viewport_size;

    double y = PAGE_PADDING;
    int best_page = 0;
    double best_overlap = -1.0;

    for (guint i = 0; i < self->page_surfaces->len; i++) {
        cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
        if (surface == NULL) continue;

        int lh = surface_logical_height(surface);
        double page_top = y;
        double page_bottom = y + lh;

        double overlap = MIN(page_bottom, scroll_bottom) - MAX(page_top, scroll_top);
        if (overlap > best_overlap) {
            best_overlap = overlap;
            best_page = (int)i;
        }

        y += lh + PAGE_GAP_BETWEEN;
    }

    if (best_page != self->current_page) {
        self->current_page = best_page;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);
    }
}

/* Coordinates from GtkGestureClick are in drawing-area content space (no adj offset needed). */
static void on_preview_pressed(GtkGestureClick *gesture, int n_press, double click_x,
                               double click_y, gpointer user_data)
{
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

    if (n_press != 1 || (state & GDK_CONTROL_MASK) == 0) goto grab_focus;

    {
        silktex_preview_render_pages(self);

        int page = -1;
        double pdf_x = 0.0;
        double pdf_y = 0.0;

        if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
            if (self->cached_surface == NULL) goto grab_focus;

            int lw = surface_logical_width(self->cached_surface);
            int lh = surface_logical_height(self->cached_surface);
            int area_w = gtk_widget_get_width(self->drawing_area);
            int area_h = gtk_widget_get_height(self->drawing_area);

            double page_x = (area_w - lw) / 2.0;
            if (page_x < 0) page_x = PAGE_PADDING;
            double page_y = MAX((double)PAGE_PADDING, (area_h - lh) / 2.0);

            if (click_x < page_x || click_x > page_x + lw || click_y < page_y ||
                click_y > page_y + lh)
                goto grab_focus;

            double pts_x = (click_x - page_x) / self->zoom;
            double pts_y = (click_y - page_y) / self->zoom;

            PopplerPage *ppage = poppler_document_get_page(self->document, self->current_page);
            if (ppage) {
                double pw, ph;
                poppler_page_get_size(ppage, &pw, &ph);
                g_object_unref(ppage);
                pdf_x = CLAMP(pts_x, 0.0, pw);
                pdf_y = CLAMP(pts_y, 0.0, ph);
            }
            page = self->current_page;

        } else if (self->layout == SILKTEX_PREVIEW_LAYOUT_CONTINUOUS &&
                   self->page_surfaces != NULL) {
            double content_y = click_y;
            double content_x = click_x;

            /* Width of the full content strip (= drawing area's allocated width). */
            int area_w = gtk_widget_get_width(self->drawing_area);

            double strip_y = PAGE_PADDING;
            for (guint i = 0; i < self->page_surfaces->len; i++) {
                cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
                if (surface == NULL) continue;

                int lw = surface_logical_width(surface);
                int lh = surface_logical_height(surface);

                double page_x = (area_w - lw) / 2.0;
                if (page_x < 0) page_x = PAGE_PADDING;

                if (content_x >= page_x && content_x <= page_x + lw && content_y >= strip_y &&
                    content_y <= strip_y + lh) {
                    double pts_x = (content_x - page_x) / self->zoom;
                    double pts_y = (content_y - strip_y) / self->zoom;

                    PopplerPage *ppage = poppler_document_get_page(self->document, (int)i);
                    if (ppage) {
                        double pw, ph;
                        poppler_page_get_size(ppage, &pw, &ph);
                        g_object_unref(ppage);
                        pdf_x = CLAMP(pts_x, 0.0, pw);
                        pdf_y = CLAMP(pts_y, 0.0, ph);
                    }
                    page = (int)i;
                    break;
                }

                strip_y += lh + PAGE_GAP_BETWEEN;
            }
        }

        if (page >= 0)
            g_signal_emit(self, signals[SIGNAL_INVERSE_SYNC_REQUESTED], 0, page, pdf_x, pdf_y);
    }

grab_focus:
    gtk_widget_grab_focus(self->scrolled_window);
}

static void on_magnifier_begin(GtkGestureDrag *g, double x, double y, gpointer ud)
{
    SilktexPreview *self = SILKTEX_PREVIEW(ud);
    self->magnifier_active = TRUE;
    self->magnifier_x = x;
    self->magnifier_y = y;
    gtk_widget_set_cursor_from_name(self->drawing_area, "zoom-in");
    gtk_widget_queue_draw(self->drawing_area);
}

static void on_magnifier_update(GtkGestureDrag *g, double dx, double dy, gpointer ud)
{
    SilktexPreview *self = SILKTEX_PREVIEW(ud);
    double sx, sy;
    gtk_gesture_drag_get_start_point(g, &sx, &sy);
    self->magnifier_x = sx + dx;
    self->magnifier_y = sy + dy;
    gtk_widget_queue_draw(self->drawing_area);
}

static void on_magnifier_end(GtkGestureDrag *g, double dx, double dy, gpointer ud)
{
    (void)g; (void)dx; (void)dy;
    SilktexPreview *self = SILKTEX_PREVIEW(ud);
    self->magnifier_active = FALSE;
    gtk_widget_set_cursor_from_name(self->drawing_area, NULL);
    gtk_widget_queue_draw(self->drawing_area);
}

static gboolean on_preview_scroll_zoom(GtkEventControllerScroll *controller, double dx, double dy,
                                       gpointer user_data)
{
    (void)dx;
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
    if ((state & GDK_CONTROL_MASK) == 0) return GDK_EVENT_PROPAGATE;
    if (dy < 0)
        silktex_preview_zoom_in(self);
    else if (dy > 0)
        silktex_preview_zoom_out(self);
    return GDK_EVENT_STOP;
}

static void silktex_preview_init(SilktexPreview *self)
{
    self->zoom = 1.0;
    self->current_page = 0;
    self->cached_page = -1;
    self->layout = SILKTEX_PREVIEW_LAYOUT_CONTINUOUS;
    self->page_surfaces = g_ptr_array_new_with_free_func((GDestroyNotify)cairo_surface_destroy);
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);

    self->scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_parent(self->scrolled_window, GTK_WIDGET(self));
    gtk_widget_add_css_class(self->scrolled_window, "silktex-preview-scroller");
    gtk_widget_set_vexpand(self->scrolled_window, TRUE);
    gtk_widget_set_hexpand(self->scrolled_window, TRUE);
    gtk_widget_set_focusable(self->scrolled_window, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scrolled_window), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(self->scrolled_window),
                                                    FALSE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(self->scrolled_window),
                                                     FALSE);

    self->drawing_area = gtk_drawing_area_new();
    gtk_widget_add_css_class(self->drawing_area, "view");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area), draw_func, self, NULL);
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self->drawing_area), 400);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self->drawing_area), 600);
    gtk_widget_set_hexpand(self->drawing_area, TRUE);
    gtk_widget_set_vexpand(self->drawing_area, TRUE);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
    gtk_widget_add_controller(self->drawing_area, GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "pressed", G_CALLBACK(on_preview_pressed), self);

    GtkGesture *mag_drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mag_drag), 3);
    gtk_widget_add_controller(self->drawing_area, GTK_EVENT_CONTROLLER(mag_drag));
    g_signal_connect(mag_drag, "drag-begin",  G_CALLBACK(on_magnifier_begin),  self);
    g_signal_connect(mag_drag, "drag-update", G_CALLBACK(on_magnifier_update), self);
    g_signal_connect(mag_drag, "drag-end",    G_CALLBACK(on_magnifier_end),    self);

    GtkEventController *scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_preview_scroll_zoom), self);
    gtk_widget_add_controller(self->scrolled_window, scroll);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled_window), self->drawing_area);

    GtkAdjustment *vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_vadj_value_changed), self);

    g_signal_connect_object(self->drawing_area, "notify::scale-factor",
                            G_CALLBACK(on_scale_factor_changed), self, G_CONNECT_SWAPPED);

    AdwStyleManager *sm = adw_style_manager_get_default();
    g_signal_connect_object(sm, "notify::dark", G_CALLBACK(on_scale_factor_changed), self,
                            G_CONNECT_SWAPPED);
}

SilktexPreview *silktex_preview_new(void)
{
    return g_object_new(SILKTEX_TYPE_PREVIEW, NULL);
}

gboolean silktex_preview_load_file(SilktexPreview *self, const char *path)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    gboolean same_path = (self->pdf_path != NULL && g_strcmp0(self->pdf_path, path) == 0);
    int saved_page = self->current_page;

    g_autofree char *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri == NULL) return FALSE;

    GError *error = NULL;
    PopplerDocument *new_doc = poppler_document_new_from_file(uri, NULL, &error);
    if (new_doc == NULL) {
        if (error) {
            g_warning("Failed to load PDF: %s", error->message);
            g_error_free(error);
        }
        return FALSE;
    }

    silktex_preview_invalidate_cache(self);
    g_clear_object(&self->document);
    self->document = new_doc;

    g_free(self->pdf_path);
    self->pdf_path = g_strdup(path);
    self->n_pages = poppler_document_get_n_pages(self->document);

    if (same_path && saved_page >= 0 && saved_page < self->n_pages)
        self->current_page = saved_page;
    else
        self->current_page = 0;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_N_PAGES]);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);

    if (self->n_pages > 0) {
        PopplerPage *p0 = poppler_document_get_page(self->document, 0);
        if (p0) {
            poppler_page_get_size(p0, &self->page_width, &self->page_height);
            g_object_unref(p0);
        }
    }

    if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_WIDTH)
        silktex_preview_zoom_fit_width(self);
    else if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_PAGE)
        silktex_preview_zoom_fit_page(self);

    gtk_widget_queue_draw(self->drawing_area);
    return TRUE;
}

void silktex_preview_refresh(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->pdf_path != NULL) {
        int saved_page = self->current_page;
        silktex_preview_load_file(self, self->pdf_path);
        if (saved_page < self->n_pages) self->current_page = saved_page;
        gtk_widget_queue_draw(self->drawing_area);
    }
}

void silktex_preview_clear(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_invalidate_cache(self);
    g_clear_object(&self->document);
    g_clear_pointer(&self->pdf_path, g_free);
    self->n_pages = 0;
    self->current_page = 0;
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_N_PAGES]);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);
    gtk_widget_queue_draw(self->drawing_area);
}

int silktex_preview_get_page(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), 0);
    return self->current_page;
}

void silktex_preview_set_page(SilktexPreview *self, int page)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    if (page < 0) page = 0;
    if (page >= self->n_pages && self->n_pages > 0) page = self->n_pages - 1;

    if (self->current_page != page) {
        self->current_page = page;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);
    }

    silktex_preview_render_pages(self);

    if (self->layout == SILKTEX_PREVIEW_LAYOUT_CONTINUOUS) {
        GtkAdjustment *vadj =
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
        if (vadj != NULL && self->page_surfaces != NULL) {
            double y = PAGE_PADDING;
            for (int i = 0; i < page; i++) {
                cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
                if (surface != NULL) y += surface_logical_height(surface);
                y += PAGE_GAP_BETWEEN;
            }
            self->scrolling_programmatically = TRUE;
            gtk_adjustment_set_value(vadj, y);
            self->scrolling_programmatically = FALSE;
        }
    } else {
        GtkAdjustment *vadj =
            gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
        if (vadj) {
            self->scrolling_programmatically = TRUE;
            gtk_adjustment_set_value(vadj, 0);
            self->scrolling_programmatically = FALSE;
        }
    }

    gtk_widget_queue_draw(self->drawing_area);
}

int silktex_preview_get_n_pages(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), 0);
    return self->n_pages;
}

void silktex_preview_next_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_page(self, self->current_page + 1);
}

void silktex_preview_prev_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_page(self, self->current_page - 1);
}

double silktex_preview_get_zoom(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), 1.0);
    return self->zoom;
}

void silktex_preview_set_zoom(SilktexPreview *self, double zoom)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    self->zoom_mode = SILKTEX_PREVIEW_ZOOM_CUSTOM;
    if (zoom < 0.1) zoom = 0.1;
    if (zoom > 10.0) zoom = 10.0;
    if (fabs(self->zoom - zoom) > 0.001) {
        self->zoom = zoom;
        silktex_preview_invalidate_cache(self);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ZOOM]);
        gtk_widget_queue_draw(self->drawing_area);
    }
}

void silktex_preview_zoom_in(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->zoom_mode != SILKTEX_PREVIEW_ZOOM_CUSTOM) return;
    silktex_preview_set_zoom(self, self->zoom * 1.2);
}

void silktex_preview_zoom_out(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->zoom_mode != SILKTEX_PREVIEW_ZOOM_CUSTOM) return;
    silktex_preview_set_zoom(self, self->zoom / 1.2);
}

static gboolean fit_zoom_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void)clock;
    (void)data;
    SilktexPreview *self = SILKTEX_PREVIEW(widget);
    if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_WIDTH) {
        silktex_preview_zoom_fit_width(self);
        return G_SOURCE_CONTINUE;
    }
    if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_PAGE) {
        silktex_preview_zoom_fit_page(self);
        return G_SOURCE_CONTINUE;
    }
    self->fit_tick_id = 0;
    return G_SOURCE_REMOVE;
}

static void ensure_fit_tick(SilktexPreview *self)
{
    if (self->fit_tick_id == 0)
        self->fit_tick_id = gtk_widget_add_tick_callback(GTK_WIDGET(self), fit_zoom_tick, NULL, NULL);
}

static gboolean on_rerender_debounce(gpointer user_data)
{
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    self->rerender_debounce_id = 0;
    silktex_preview_invalidate_cache(self);
    gtk_widget_queue_draw(self->drawing_area);
    return G_SOURCE_REMOVE;
}

static void schedule_rerender_debounce(SilktexPreview *self)
{
    if (self->rerender_debounce_id)
        g_source_remove(self->rerender_debounce_id);
    self->rerender_debounce_id = g_timeout_add(50, on_rerender_debounce, self);
}

void silktex_preview_zoom_fit_width(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    self->zoom_mode = SILKTEX_PREVIEW_ZOOM_FIT_WIDTH;
    ensure_fit_tick(self);
    if (self->document == NULL) return;
    int widget_width = gtk_widget_get_width(GTK_WIDGET(self));
    if (widget_width > 0 && self->page_width > 0) {
        double new_zoom = (widget_width - 2 * PAGE_PADDING) / self->page_width;
        if (new_zoom < 0.1) new_zoom = 0.1;
        if (new_zoom > 10.0) new_zoom = 10.0;
        if (fabs(self->zoom - new_zoom) > 0.001) {
            self->zoom = new_zoom;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ZOOM]);
            /* Update content dims now so the scrolled window doesn't show a
             * spurious scrollbar while the debounce re-render is pending. */
            int new_lw = (int)ceil(self->page_width * self->zoom);
            int new_lh = (int)ceil(self->page_height * self->zoom);
            gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self->drawing_area),
                                               MAX(new_lw + 2 * PAGE_PADDING, 1));
            if (self->n_pages > 0) {
                int est_h = self->n_pages * new_lh
                            + (self->n_pages > 1 ? (self->n_pages - 1) * PAGE_GAP_BETWEEN : 0)
                            + 2 * PAGE_PADDING;
                gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self->drawing_area),
                                                    MAX(est_h, 1));
            }
            gtk_widget_queue_draw(self->drawing_area);
            schedule_rerender_debounce(self);
        }
    }
}

void silktex_preview_zoom_fit_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    self->zoom_mode = SILKTEX_PREVIEW_ZOOM_FIT_PAGE;
    ensure_fit_tick(self);
    if (self->document == NULL) return;
    int widget_width = gtk_widget_get_width(GTK_WIDGET(self));
    int widget_height = gtk_widget_get_height(GTK_WIDGET(self));
    if (widget_width > 0 && widget_height > 0 && self->page_width > 0 && self->page_height > 0) {
        double zoom_w = (widget_width - 2 * PAGE_PADDING) / self->page_width;
        double zoom_h = (widget_height - 2 * PAGE_PADDING) / self->page_height;
        double new_zoom = MIN(zoom_w, zoom_h);
        if (new_zoom < 0.1) new_zoom = 0.1;
        if (new_zoom > 10.0) new_zoom = 10.0;
        if (fabs(self->zoom - new_zoom) > 0.001) {
            self->zoom = new_zoom;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ZOOM]);
            int new_lw = (int)ceil(self->page_width * self->zoom);
            int new_lh = (int)ceil(self->page_height * self->zoom);
            gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(self->drawing_area),
                                               MAX(new_lw + 2 * PAGE_PADDING, 1));
            if (self->n_pages > 0) {
                int est_h = self->n_pages * new_lh
                            + (self->n_pages > 1 ? (self->n_pages - 1) * PAGE_GAP_BETWEEN : 0)
                            + 2 * PAGE_PADDING;
                gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(self->drawing_area),
                                                    MAX(est_h, 1));
            }
            gtk_widget_queue_draw(self->drawing_area);
            schedule_rerender_debounce(self);
        }
    }
}

void silktex_preview_toggle_zoom_fit_width(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_WIDTH) {
        self->zoom_mode = SILKTEX_PREVIEW_ZOOM_CUSTOM;
    } else {
        silktex_preview_zoom_fit_width(self);
    }
}

void silktex_preview_toggle_zoom_fit_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->zoom_mode == SILKTEX_PREVIEW_ZOOM_FIT_PAGE) {
        self->zoom_mode = SILKTEX_PREVIEW_ZOOM_CUSTOM;
    } else {
        silktex_preview_zoom_fit_page(self);
    }
}

SilktexPreviewZoomMode silktex_preview_get_zoom_mode(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), SILKTEX_PREVIEW_ZOOM_CUSTOM);
    return self->zoom_mode;
}

SilktexPreviewLayout silktex_preview_get_layout(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), SILKTEX_PREVIEW_LAYOUT_CONTINUOUS);
    return self->layout;
}

void silktex_preview_set_layout(SilktexPreview *self, SilktexPreviewLayout layout)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->layout == layout) return;
    self->layout = layout;
    silktex_preview_invalidate_cache(self);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LAYOUT]);
    GtkAdjustment *vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    if (vadj) {
        self->scrolling_programmatically = TRUE;
        gtk_adjustment_set_value(vadj, 0);
        self->scrolling_programmatically = FALSE;
    }
    gtk_widget_queue_draw(self->drawing_area);
}

void silktex_preview_set_inverted(SilktexPreview *self, gboolean inverted)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    if (self->inverted == inverted) return;
    self->inverted = inverted;
    gtk_widget_queue_draw(self->drawing_area);
}

gboolean silktex_preview_get_inverted(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), FALSE);
    return self->inverted;
}

/* pdf_y uses bottom-left origin — flip to top-left canvas coords before scrolling. */
void silktex_preview_scroll_to_position(SilktexPreview *self, double pdf_x, double pdf_y)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    silktex_preview_render_pages(self);

    GtkAdjustment *hadj =
        gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    GtkAdjustment *vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    if (!hadj || !vadj) return;

    int area_w = gtk_widget_get_width(self->drawing_area);

    if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
        if (self->cached_surface == NULL) return;

        int lw = surface_logical_width(self->cached_surface);
        int lh = surface_logical_height(self->cached_surface);

        double page_x = (area_w - lw) / 2.0;
        if (page_x < 0) page_x = PAGE_PADDING;

        double canvas_x = page_x + pdf_x * self->zoom;
        double canvas_y = PAGE_PADDING + (self->page_height - pdf_y) * self->zoom;
        (void)lh;

        double viewport_h = gtk_adjustment_get_page_size(vadj);
        double viewport_w = gtk_adjustment_get_page_size(hadj);

        self->scrolling_programmatically = TRUE;
        gtk_adjustment_set_value(vadj, canvas_y - viewport_h / 2.0);
        gtk_adjustment_set_value(hadj, canvas_x - viewport_w / 2.0);
        self->scrolling_programmatically = FALSE;

    } else if (self->layout == SILKTEX_PREVIEW_LAYOUT_CONTINUOUS && self->page_surfaces != NULL &&
               self->current_page < (int)self->page_surfaces->len) {
        double strip_y = PAGE_PADDING;
        for (int i = 0; i < self->current_page; i++) {
            cairo_surface_t *s = g_ptr_array_index(self->page_surfaces, i);
            if (s) strip_y += surface_logical_height(s);
            strip_y += PAGE_GAP_BETWEEN;
        }

        cairo_surface_t *cur = g_ptr_array_index(self->page_surfaces, self->current_page);
        int lw = cur ? surface_logical_width(cur) : 0;
        int lh = cur ? surface_logical_height(cur) : 0;

        double page_x = (area_w - lw) / 2.0;
        if (page_x < 0) page_x = PAGE_PADDING;

        double canvas_x = page_x + pdf_x * self->zoom;
        double canvas_y = strip_y + (self->page_height - pdf_y) * self->zoom;
        (void)lh;

        double viewport_h = gtk_adjustment_get_page_size(vadj);
        double viewport_w = gtk_adjustment_get_page_size(hadj);

        self->scrolling_programmatically = TRUE;
        gtk_adjustment_set_value(vadj, canvas_y - viewport_h / 2.0);
        gtk_adjustment_set_value(hadj, canvas_x - viewport_w / 2.0);
        self->scrolling_programmatically = FALSE;
    }
}