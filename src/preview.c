/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "preview.h"
#include <adwaita.h>
#include <poppler.h>
#include <math.h>

/* Outer breathing room around every rendered page.  Requested by the
 * user — keeps each page clearly separated from its neighbours and from
 * the preview background on all sides. */
#define PAGE_PADDING 6
#define PAGE_GAP_BETWEEN 20

struct _SilktexPreview {
    GtkWidget parent_instance;

    GtkWidget *scrolled_window;
    GtkWidget *drawing_area;

    PopplerDocument *document;
    char *pdf_path;

    int current_page;
    int n_pages;
    double zoom;

    double page_width;
    double page_height;

    cairo_surface_t *cached_surface;
    int cached_page;
    double cached_zoom;
    int cached_scale; /* device-pixel scale factor used for the cache */
    GPtrArray *page_surfaces;
    double total_height;

    SilktexPreviewLayout layout;
    gboolean scrolling_programmatically;
    gulong scale_factor_handler;
};

G_DEFINE_FINAL_TYPE (SilktexPreview, silktex_preview, GTK_TYPE_WIDGET)

    enum { PROP_0, PROP_PAGE, PROP_N_PAGES, PROP_ZOOM, PROP_LAYOUT, N_PROPS };

static GParamSpec *properties[N_PROPS];

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

static void on_scale_factor_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)obj;
    (void)pspec;
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    silktex_preview_invalidate_cache(self);
    gtk_widget_queue_draw(self->drawing_area);
}

/*
 * Render a single page into an image surface.
 *
 * HiDPI: the surface is created at `zoom * device_scale` pixels but
 * tagged via cairo_surface_set_device_scale() so Cairo treats each
 * logical pixel as `device_scale` device pixels when compositing.  The
 * caller (draw_func) therefore keeps working in logical coordinates —
 * width / height as observed by cairo_image_surface_get_width() divided
 * by the device scale — while Poppler rasterizes at the panel's true
 * resolution, eliminating the upscaling blur on Retina / 200% displays.
 */
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

/* Logical width/height of a surface (accounts for its device scale). */
static int surface_logical_width(cairo_surface_t *s)
{
    if (!s) return 0;
    double sx = 1, sy = 1;
    cairo_surface_get_device_scale(s, &sx, &sy);
    double w = cairo_image_surface_get_width(s);
    return (int)(w / (sx > 0 ? sx : 1.0));
}

static int surface_logical_height(cairo_surface_t *s)
{
    if (!s) return 0;
    double sx = 1, sy = 1;
    cairo_surface_get_device_scale(s, &sx, &sy);
    double h = cairo_image_surface_get_height(s);
    return (int)(h / (sy > 0 ? sy : 1.0));
}

static void silktex_preview_render_pages(SilktexPreview *self)
{
    if (self->document == NULL) return;
    if (self->n_pages <= 0) return;

    const int page_gap = PAGE_GAP_BETWEEN;
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(self));
    if (scale < 1) scale = 1;

    if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
        /* Cache is keyed by page + zoom + scale. */
        if (self->cached_surface != NULL && self->cached_page == self->current_page &&
            self->cached_scale == scale && fabs(self->cached_zoom - self->zoom) < 0.001) {
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
        gtk_widget_set_size_request(self->drawing_area, MAX(lw + 2 * PAGE_PADDING, 1),
                                    MAX((int)self->total_height, 1));
        return;
    }

    /* Continuous: render all pages (cache keyed by zoom + scale). */
    if (self->page_surfaces != NULL && self->page_surfaces->len == (guint)self->n_pages &&
        self->cached_scale == scale && fabs(self->cached_zoom - self->zoom) < 0.001) {
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

    if (self->n_pages > 1) {
        self->total_height += (self->n_pages - 1) * page_gap;
    }
    self->total_height += 2 * PAGE_PADDING;
    gtk_widget_set_size_request(self->drawing_area, MAX(max_width + 2 * PAGE_PADDING, 1),
                                MAX((int)self->total_height, 1));
}

/* Pick a preview background color that pairs well with the current
 * Adwaita light/dark scheme.  The user reported the fixed dark grey
 * being too dark in light mode; we now use a subtle neutral grey there
 * and a slightly warmer charcoal in dark mode (matches Evince / Papers). */
static void preview_bg_color(double *r, double *g, double *b)
{
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

static void draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);

    double bg_r, bg_g, bg_b;
    preview_bg_color(&bg_r, &bg_g, &bg_b);
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

    /* Page-border colour — subtle divider that works on both themes. */
    const double border = 0.55;

    if (self->layout == SILKTEX_PREVIEW_LAYOUT_SINGLE_PAGE) {
        if (self->cached_surface == NULL) return;
        int lw = surface_logical_width(self->cached_surface);
        int lh = surface_logical_height(self->cached_surface);
        double x = (width - lw) / 2.0;
        if (x < 0) x = PAGE_PADDING;
        double y = PAGE_PADDING;

        cairo_set_source_surface(cr, self->cached_surface, x, y);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);

        cairo_set_source_rgb(cr, border, border, border);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x - 0.5, y - 0.5, lw + 1, lh + 1);
        cairo_stroke(cr);
        return;
    }

    double y = PAGE_PADDING;
    for (guint i = 0; i < self->page_surfaces->len; i++) {
        cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
        if (surface == NULL) continue;
        int lw = surface_logical_width(surface);
        int lh = surface_logical_height(surface);

        double x = (width - lw) / 2.0;
        if (x < 0) x = PAGE_PADDING;

        cairo_set_source_surface(cr, surface, x, y);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);

        cairo_set_source_rgb(cr, border, border, border);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x - 0.5, y - 0.5, lw + 1, lh + 1);
        cairo_stroke(cr);

        y += lh + PAGE_GAP_BETWEEN;
    }
}

static void silktex_preview_dispose(GObject *object)
{
    SilktexPreview *self = SILKTEX_PREVIEW(object);

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

static void silktex_preview_class_init(SilktexPreviewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = silktex_preview_dispose;
    object_class->get_property = silktex_preview_get_property;
    object_class->set_property = silktex_preview_set_property;

    properties[PROP_PAGE] = g_param_spec_int("page", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READWRITE);
    properties[PROP_N_PAGES] =
        g_param_spec_int("n-pages", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE);
    properties[PROP_ZOOM] =
        g_param_spec_double("zoom", NULL, NULL, 0.1, 10.0, 1.0, G_PARAM_READWRITE);
    properties[PROP_LAYOUT] = g_param_spec_int("layout", NULL, NULL, 0, 1, 0, G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPS, properties);

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void on_vadj_value_changed(GtkAdjustment *adj, gpointer user_data)
{
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    if (self->scrolling_programmatically) return;
    if (self->layout != SILKTEX_PREVIEW_LAYOUT_CONTINUOUS) return;
    if (self->document == NULL || self->page_surfaces == NULL) return;
    if (self->page_surfaces->len == 0) return;

    const int page_gap = PAGE_GAP_BETWEEN;
    double value = gtk_adjustment_get_value(adj);
    double page_size = gtk_adjustment_get_page_size(adj);
    double center = value + page_size / 2.0;

    double y = PAGE_PADDING;
    int page = 0;
    for (guint i = 0; i < self->page_surfaces->len; i++) {
        cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
        if (surface == NULL) continue;
        int h = surface_logical_height(surface);
        if (center < y + h + page_gap / 2.0) {
            page = i;
            break;
        }
        y += h + page_gap;
        page = i;
    }

    if (page != self->current_page) {
        self->current_page = page;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);
    }
}

static void on_preview_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                               gpointer user_data)
{
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);
    gtk_widget_grab_focus(self->scrolled_window);
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
    gtk_widget_set_vexpand(self->scrolled_window, TRUE);
    gtk_widget_set_hexpand(self->scrolled_window, TRUE);
    gtk_widget_set_focusable(self->scrolled_window, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->scrolled_window), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    self->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area), draw_func, self, NULL);
    gtk_widget_set_size_request(self->drawing_area, 400, 600);
    gtk_widget_set_hexpand(self->drawing_area, TRUE);
    gtk_widget_set_vexpand(self->drawing_area, TRUE);

    GtkGesture *click = gtk_gesture_click_new();
    gtk_widget_add_controller(self->drawing_area, GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "pressed", G_CALLBACK(on_preview_pressed), self);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled_window), self->drawing_area);

    GtkAdjustment *vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_vadj_value_changed), self);

    /* Re-render when the widget lands on a different-DPI monitor. */
    self->scale_factor_handler =
        g_signal_connect(self, "notify::scale-factor", G_CALLBACK(on_scale_factor_changed), self);

    /* Redraw the background when the Adwaita theme flips light↔dark so
     * the preview pane tracks the rest of the application. */
    AdwStyleManager *sm = adw_style_manager_get_default();
    g_signal_connect_object(sm, "notify::dark", G_CALLBACK(on_scale_factor_changed), self,
                            G_CONNECT_DEFAULT);
}

SilktexPreview *silktex_preview_new(void)
{
    return g_object_new(SILKTEX_TYPE_PREVIEW, NULL);
}

gboolean silktex_preview_load_file(SilktexPreview *self, const char *path)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    /*
     * Reloading the same path (e.g. after every auto-compile) should feel
     * like a refresh — preserve the current page and scroll position
     * rather than snapping back to page 1.  We also keep the previous
     * document loaded if the new file can't be opened so the preview
     * keeps showing the last successful render.
     */
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

    if (same_path && saved_page >= 0 && saved_page < self->n_pages) {
        self->current_page = saved_page;
    } else {
        self->current_page = 0;
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_N_PAGES]);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);

    gtk_widget_queue_draw(self->drawing_area);
    return TRUE;
}

void silktex_preview_refresh(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    if (self->pdf_path != NULL) {
        int saved_page = self->current_page;
        silktex_preview_load_file(self, self->pdf_path);
        if (saved_page < self->n_pages) {
            self->current_page = saved_page;
        }
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
            const int page_gap = PAGE_GAP_BETWEEN;
            double y = PAGE_PADDING;
            for (int i = 0; i < page; i++) {
                cairo_surface_t *surface = g_ptr_array_index(self->page_surfaces, i);
                if (surface != NULL) {
                    y += surface_logical_height(surface);
                }
                y += page_gap;
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
    silktex_preview_set_zoom(self, self->zoom * 1.2);
}

void silktex_preview_zoom_out(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_zoom(self, self->zoom / 1.2);
}

void silktex_preview_zoom_fit_width(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    if (self->document == NULL) return;

    int widget_width = gtk_widget_get_width(self->scrolled_window);
    if (widget_width > 0 && self->page_width > 0) {
        double new_zoom = (widget_width - 40) / self->page_width;
        silktex_preview_set_zoom(self, new_zoom);
    }
}

void silktex_preview_zoom_fit_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    if (self->document == NULL) return;

    int widget_width = gtk_widget_get_width(self->scrolled_window);
    int widget_height = gtk_widget_get_height(self->scrolled_window);

    if (widget_width > 0 && widget_height > 0 && self->page_width > 0 && self->page_height > 0) {
        double zoom_w = (widget_width - 40) / self->page_width;
        double zoom_h = (widget_height - 40) / self->page_height;
        silktex_preview_set_zoom(self, MIN(zoom_w, zoom_h));
    }
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

void silktex_preview_scroll_to_position(SilktexPreview *self, double x, double y)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    GtkAdjustment *hadj =
        gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    GtkAdjustment *vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));

    gtk_adjustment_set_value(hadj, x * self->zoom);
    gtk_adjustment_set_value(vadj, y * self->zoom);
}
