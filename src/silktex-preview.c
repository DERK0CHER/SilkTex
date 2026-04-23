/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#include "silktex-preview.h"
#include <poppler.h>
#include <math.h>

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
};

G_DEFINE_FINAL_TYPE(SilktexPreview, silktex_preview, GTK_TYPE_WIDGET)

enum {
    PROP_0,
    PROP_PAGE,
    PROP_N_PAGES,
    PROP_ZOOM,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
silktex_preview_invalidate_cache(SilktexPreview *self)
{
    g_clear_pointer(&self->cached_surface, cairo_surface_destroy);
    self->cached_page = -1;
    self->cached_zoom = 0;
}

static void
silktex_preview_render_page(SilktexPreview *self)
{
    if (self->document == NULL) return;
    if (self->current_page < 0 || self->current_page >= self->n_pages) return;

    if (self->cached_surface != NULL &&
        self->cached_page == self->current_page &&
        fabs(self->cached_zoom - self->zoom) < 0.001) {
        return;
    }

    silktex_preview_invalidate_cache(self);

    PopplerPage *page = poppler_document_get_page(self->document, self->current_page);
    if (page == NULL) return;

    poppler_page_get_size(page, &self->page_width, &self->page_height);

    int width = (int)(self->page_width * self->zoom);
    int height = (int)(self->page_height * self->zoom);

    self->cached_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(self->cached_surface);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    cairo_scale(cr, self->zoom, self->zoom);
    poppler_page_render(page, cr);

    cairo_destroy(cr);
    g_object_unref(page);

    self->cached_page = self->current_page;
    self->cached_zoom = self->zoom;

    gtk_widget_set_size_request(self->drawing_area, width, height);
}

static void
draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    SilktexPreview *self = SILKTEX_PREVIEW(user_data);

    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_paint(cr);

    if (self->document == NULL) {
        cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 16);

        const char *text = "No PDF loaded";
        cairo_text_extents_t extents;
        cairo_text_extents(cr, text, &extents);
        cairo_move_to(cr, (width - extents.width) / 2, (height + extents.height) / 2);
        cairo_show_text(cr, text);
        return;
    }

    silktex_preview_render_page(self);

    if (self->cached_surface != NULL) {
        int surf_width = cairo_image_surface_get_width(self->cached_surface);
        int surf_height = cairo_image_surface_get_height(self->cached_surface);

        double x = (width - surf_width) / 2.0;
        double y = 0;

        if (x < 0) x = 0;
        if (y < 0) y = 0;

        cairo_set_source_surface(cr, self->cached_surface, x, y);
        cairo_paint(cr);

        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x - 0.5, y - 0.5, surf_width + 1, surf_height + 1);
        cairo_stroke(cr);
    }
}

static void
silktex_preview_dispose(GObject *object)
{
    SilktexPreview *self = SILKTEX_PREVIEW(object);

    silktex_preview_invalidate_cache(self);
    g_clear_object(&self->document);
    g_clear_pointer(&self->pdf_path, g_free);

    gtk_widget_unparent(self->scrolled_window);

    G_OBJECT_CLASS(silktex_preview_parent_class)->dispose(object);
}

static void
silktex_preview_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
silktex_preview_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    SilktexPreview *self = SILKTEX_PREVIEW(object);

    switch (prop_id) {
        case PROP_PAGE:
            silktex_preview_set_page(self, g_value_get_int(value));
            break;
        case PROP_ZOOM:
            silktex_preview_set_zoom(self, g_value_get_double(value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
silktex_preview_class_init(SilktexPreviewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = silktex_preview_dispose;
    object_class->get_property = silktex_preview_get_property;
    object_class->set_property = silktex_preview_set_property;

    properties[PROP_PAGE] =
        g_param_spec_int("page", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READWRITE);
    properties[PROP_N_PAGES] =
        g_param_spec_int("n-pages", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE);
    properties[PROP_ZOOM] =
        g_param_spec_double("zoom", NULL, NULL, 0.1, 10.0, 1.0, G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPS, properties);

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
silktex_preview_init(SilktexPreview *self)
{
    self->zoom = 1.0;
    self->current_page = 0;
    self->cached_page = -1;

    self->scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_parent(self->scrolled_window, GTK_WIDGET(self));
    gtk_widget_set_vexpand(self->scrolled_window, TRUE);
    gtk_widget_set_hexpand(self->scrolled_window, TRUE);

    self->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area), draw_func, self, NULL);
    gtk_widget_set_size_request(self->drawing_area, 400, 600);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled_window), self->drawing_area);
}

SilktexPreview *
silktex_preview_new(void)
{
    return g_object_new(SILKTEX_TYPE_PREVIEW, NULL);
}

gboolean
silktex_preview_load_file(SilktexPreview *self, const char *path)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    silktex_preview_invalidate_cache(self);
    g_clear_object(&self->document);

    g_autofree char *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri == NULL) return FALSE;

    GError *error = NULL;
    self->document = poppler_document_new_from_file(uri, NULL, &error);

    if (self->document == NULL) {
        g_warning("Failed to load PDF: %s", error->message);
        g_error_free(error);
        return FALSE;
    }

    g_free(self->pdf_path);
    self->pdf_path = g_strdup(path);
    self->n_pages = poppler_document_get_n_pages(self->document);
    self->current_page = 0;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_N_PAGES]);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);

    gtk_widget_queue_draw(self->drawing_area);
    return TRUE;
}

void
silktex_preview_refresh(SilktexPreview *self)
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

void
silktex_preview_clear(SilktexPreview *self)
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

int
silktex_preview_get_page(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), 0);
    return self->current_page;
}

void
silktex_preview_set_page(SilktexPreview *self, int page)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    if (page < 0) page = 0;
    if (page >= self->n_pages && self->n_pages > 0) page = self->n_pages - 1;

    if (self->current_page != page) {
        self->current_page = page;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PAGE]);
        gtk_widget_queue_draw(self->drawing_area);
    }
}

int
silktex_preview_get_n_pages(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), 0);
    return self->n_pages;
}

void
silktex_preview_next_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_page(self, self->current_page + 1);
}

void
silktex_preview_prev_page(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_page(self, self->current_page - 1);
}

double
silktex_preview_get_zoom(SilktexPreview *self)
{
    g_return_val_if_fail(SILKTEX_IS_PREVIEW(self), 1.0);
    return self->zoom;
}

void
silktex_preview_set_zoom(SilktexPreview *self, double zoom)
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

void
silktex_preview_zoom_in(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_zoom(self, self->zoom * 1.2);
}

void
silktex_preview_zoom_out(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));
    silktex_preview_set_zoom(self, self->zoom / 1.2);
}

void
silktex_preview_zoom_fit_width(SilktexPreview *self)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    if (self->document == NULL) return;

    int widget_width = gtk_widget_get_width(self->scrolled_window);
    if (widget_width > 0 && self->page_width > 0) {
        double new_zoom = (widget_width - 40) / self->page_width;
        silktex_preview_set_zoom(self, new_zoom);
    }
}

void
silktex_preview_zoom_fit_page(SilktexPreview *self)
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

void
silktex_preview_scroll_to_position(SilktexPreview *self, double x, double y)
{
    g_return_if_fail(SILKTEX_IS_PREVIEW(self));

    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->scrolled_window));

    gtk_adjustment_set_value(hadj, x * self->zoom);
    gtk_adjustment_set_value(vadj, y * self->zoom);
}
