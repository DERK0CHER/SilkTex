/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Primary ("hamburger") menu popover: theme swatches, file shortcuts, Git
 * entries, preferences, shortcuts window, about, quit. Also registers the
 * related GActions (set-theme, show-recent, shortcuts, open-menu).
 *
 * Theme model:
 *   - Adwaita color scheme (light / dark) comes from AdwStyleManager and is
 *     driven by config "Interface"/"theme" (follow | light | dark).
 *   - Editor GtkSourceView scheme is separate — silktex_window_apply_theme_to_*
 *     in window.c resolves Gruvbox / lights-out / user preference.
 *
 * Popover buttons activate named actions (e.g. win.new) via a one-idle-tick
 * deferral so the popover can close before the action runs (avoids nesting
 * and focus glitches).
 */

#include "window-private.h"
#include "configfile.h"
#include "i18n.h"

/* -------------------------------------------------------------------------- */
/* AdwStyleManager — shell chrome only */

void silktex_window_apply_theme_from_config(void)
{
    const char *mode = config_get_string("Interface", "theme");
    AdwStyleManager *sm = adw_style_manager_get_default();
    if (g_strcmp0(mode, "light") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
    else if (g_strcmp0(mode, "dark") == 0)
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);
    else
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
}

static void update_theme_buttons(SilktexWindow *self, const char *mode)
{
    if (!mode || !*mode) mode = "follow";
    if (self->theme_follow)
        gtk_toggle_button_set_active(self->theme_follow, g_strcmp0(mode, "follow") == 0);
    if (self->theme_light)
        gtk_toggle_button_set_active(self->theme_light, g_strcmp0(mode, "light") == 0);
    if (self->theme_dark)
        gtk_toggle_button_set_active(self->theme_dark, g_strcmp0(mode, "dark") == 0);
}

/* Stateful action — default state "'follow'" is set from config in window init. */

static void change_theme(GSimpleAction *action, GVariant *value, gpointer ud)
{
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    const char *mode = g_variant_get_string(value, NULL);
    if (g_strcmp0(mode, "light") != 0 && g_strcmp0(mode, "dark") != 0 &&
        g_strcmp0(mode, "follow") != 0)
        mode = "follow";
    config_set_string("Interface", "theme", mode);
    config_save();
    silktex_window_apply_theme_from_config();
    g_simple_action_set_state(action, g_variant_new_string(mode));
    update_theme_buttons(self, mode);
    silktex_window_apply_theme_to_all_editors(self);
    silktex_window_apply_preview_theme(self);
}

static void on_system_dark_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (g_strcmp0(config_get_string("Interface", "theme"), "follow") == 0) {
        silktex_window_apply_theme_to_all_editors(self);
        silktex_window_apply_preview_theme(self);
    }
}

void silktex_window_connect_theme_follow(SilktexWindow *self)
{
    g_signal_connect_object(adw_style_manager_get_default(), "notify::dark",
                            G_CALLBACK(on_system_dark_changed), self, G_CONNECT_DEFAULT);
}

/* -------------------------------------------------------------------------- */
/* Recent files dialog — GtkRecentManager, LaTeX-like MIME and extensions only */

typedef struct {
    SilktexWindow *self;
    AdwDialog *dialog;
    char *uri;
} RecentItem;

static void recent_item_free(gpointer data)
{
    RecentItem *r = data;
    g_free(r->uri);
    g_free(r);
}

static void on_recent_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer ud)
{
    (void)box;
    RecentItem *r = g_object_get_data(G_OBJECT(row), "recent-item");
    if (!r || !r->uri) return;
    GFile *file = g_file_new_for_uri(r->uri);
    silktex_window_open_file(r->self, file);
    g_object_unref(file);
    if (r->dialog) adw_dialog_close(r->dialog);
}

static void on_recent_clear(GtkButton *btn, gpointer ud)
{
    (void)btn;
    AdwDialog *dlg = ADW_DIALOG(ud);
    GtkRecentManager *mgr = gtk_recent_manager_get_default();
    gtk_recent_manager_purge_items(mgr, NULL);
    adw_dialog_close(dlg);
}

static void action_show_recent(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);

    AdwDialog *dlg = adw_dialog_new();
    adw_dialog_set_title(dlg, _("Open Recent"));
    adw_dialog_set_content_width(dlg, 520);
    adw_dialog_set_content_height(dlg, 440);

    GtkWidget *toolbarview = adw_toolbar_view_new();
    GtkWidget *hb = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbarview), hb);

    GtkWidget *clear = gtk_button_new_with_label(_("Clear"));
    gtk_widget_add_css_class(clear, "flat");
    g_signal_connect(clear, "clicked", G_CALLBACK(on_recent_clear), dlg);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), clear);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "navigation-sidebar");
    g_signal_connect(list, "row-activated", G_CALLBACK(on_recent_row_activated), self);

    GtkRecentManager *mgr = gtk_recent_manager_get_default();
    GList *items = gtk_recent_manager_get_items(mgr);
    int added = 0;
    for (GList *l = items; l != NULL; l = l->next) {
        GtkRecentInfo *info = l->data;
        const char *uri = gtk_recent_info_get_uri(info);
        if (!uri) continue;
        const char *mime = gtk_recent_info_get_mime_type(info);
        const char *name = gtk_recent_info_get_display_name(info);
        if (!g_str_has_prefix(uri, "file://")) continue;
        gboolean is_tex = FALSE;
        if (g_str_has_suffix(uri, ".tex") || g_str_has_suffix(uri, ".ltx") ||
            g_str_has_suffix(uri, ".sty") || g_str_has_suffix(uri, ".cls"))
            is_tex = TRUE;
        if (mime &&
            (g_str_has_prefix(mime, "text/x-tex") || g_str_has_prefix(mime, "text/x-latex")))
            is_tex = TRUE;
        if (!is_tex) continue;

        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                      name ? name : gtk_recent_info_get_short_name(info));
        g_autofree char *path = g_filename_from_uri(uri, NULL, NULL);
        if (path) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), path);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);

        RecentItem *ri = g_new0(RecentItem, 1);
        ri->self = self;
        ri->dialog = dlg;
        ri->uri = g_strdup(uri);
        g_object_set_data_full(G_OBJECT(row), "recent-item", ri, recent_item_free);

        gtk_list_box_append(GTK_LIST_BOX(list), row);
        added++;
        if (added >= 40) break;
    }
    g_list_free_full(items, (GDestroyNotify)gtk_recent_info_unref);

    if (added == 0) {
        GtkWidget *status = adw_status_page_new();
        adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "document-open-recent-symbolic");
        adw_status_page_set_title(ADW_STATUS_PAGE(status), _("No Recent Files"));
        adw_status_page_set_description(ADW_STATUS_PAGE(status),
                                        _("Recently opened LaTeX files will appear here."));
        adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), status);
    } else {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
        adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbarview), scroll);
    }

    adw_dialog_set_child(dlg, toolbarview);
    adw_dialog_present(dlg, GTK_WIDGET(self));
}

/*
 * GtkShortcutsWindow is deprecated in GTK 4.18+ with no in-tree replacement;
 * we keep it for now for a native GNOME-style overlay. Build with
 * G_GNUC_BEGIN_IGNORE_DEPRECATIONS around the block.
 */

static GtkWidget *make_shortcut(const char *accel, const char *title)
{
    return g_object_new(GTK_TYPE_SHORTCUTS_SHORTCUT, "accelerator", accel, "title", title, NULL);
}

static GtkWidget *make_group(const char *title)
{
    return g_object_new(GTK_TYPE_SHORTCUTS_GROUP, "title", title, NULL);
}

static void action_shortcuts(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *win = g_object_new(GTK_TYPE_SHORTCUTS_WINDOW, "modal", TRUE, "transient-for", self,
                                  "destroy-with-parent", TRUE, NULL);

    GtkWidget *section = g_object_new(GTK_TYPE_SHORTCUTS_SECTION, "section-name", "main",
                                      "visible", TRUE, NULL);

    GtkWidget *g_files = make_group(_("Files"));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_files),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>n", _("New tab"))));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_files),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>o", _("Open"))));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_files),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>s", _("Save"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_files),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary><Shift>s", _("Save As"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_files),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>q", _("Quit"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_files));

    GtkWidget *g_edit = make_group(_("Editing"));
    gtk_shortcuts_group_add_shortcut(GTK_SHORTCUTS_GROUP(g_edit),
                                     GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>z", _("Undo"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary><Shift>z", _("Redo"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>b", _("Bold"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>i", _("Italic"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>u", _("Underline"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>f", _("Find"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_edit),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>h", _("Find & Replace"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_edit));

    GtkWidget *g_doc = make_group(_("Document"));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_doc),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>Return", _("Compile"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_doc),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary><Shift>f", _("Forward SyncTeX"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_doc));

    GtkWidget *g_view = make_group(_("View"));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>plus", _("Zoom in"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>minus", _("Zoom out"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>0", _("Fit width"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F8", _("Toggle outline"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F9", _("Toggle preview"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F10", _("Main menu"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("F11", _("Fullscreen"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>question", _("Keyboard shortcuts"))));
    gtk_shortcuts_group_add_shortcut(
        GTK_SHORTCUTS_GROUP(g_view),
        GTK_SHORTCUTS_SHORTCUT(make_shortcut("<Primary>comma", _("Preferences"))));
    gtk_shortcuts_section_add_group(GTK_SHORTCUTS_SECTION(section), GTK_SHORTCUTS_GROUP(g_view));

    gtk_shortcuts_window_add_section(GTK_SHORTCUTS_WINDOW(win), GTK_SHORTCUTS_SECTION(section));
    gtk_window_present(GTK_WINDOW(win));
    G_GNUC_END_IGNORE_DEPRECATIONS
}

/*
 * Popover menu actions must not run while the popover is tearing down — we
 * pop down first, then gtk_widget_activate_action on the next main-loop idle.
 */

typedef struct {
    SilktexWindow *self;
    char *action;
} DeferredAction;

static gboolean activate_deferred_action(gpointer user_data)
{
    DeferredAction *data = user_data;
    gtk_widget_activate_action(GTK_WIDGET(data->self), data->action, NULL);
    g_object_unref(data->self);
    g_free(data->action);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void on_primary_popover_action(GtkButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    const char *action = g_object_get_data(G_OBJECT(button), "silktex-action");

    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }

    if (action != NULL) {
        DeferredAction *data = g_new0(DeferredAction, 1);
        data->self = g_object_ref(self);
        data->action = g_strdup(action);
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, activate_deferred_action, data, NULL);
    }
}

/* Pick the first icon name present in the theme — some themes omit vcs-* / emblem-* names. */

static void silktex_image_set_icon_list(GtkImage *image, const char *const *candidates)
{
    if (candidates == NULL || candidates[0] == NULL) return;
    GtkIconTheme *t = gtk_icon_theme_get_for_display(gdk_display_get_default());
    for (int i = 0; candidates[i]; i++) {
        if (gtk_icon_theme_has_icon(t, candidates[i])) {
            gtk_image_set_from_icon_name(image, candidates[i]);
            return;
        }
    }
    gtk_image_set_from_icon_name(image, candidates[0]);
}

static GtkWidget *make_primary_popover_with_icons(const char *label, const char *const *icon_candidates,
                                                    const char *action, SilktexWindow *self)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_hexpand(button, TRUE);
    g_object_set_data_full(G_OBJECT(button), "silktex-action", g_strdup(action), g_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *icon = gtk_image_new();
    silktex_image_set_icon_list(GTK_IMAGE(icon), icon_candidates);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *lbl = gtk_label_new_with_mnemonic(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(box), lbl);

    gtk_button_set_child(GTK_BUTTON(button), box);
    g_signal_connect(button, "clicked", G_CALLBACK(on_primary_popover_action), self);
    return button;
}

static GtkWidget *make_primary_popover_button(const char *label, const char *icon_name,
                                              const char *action, SilktexWindow *self)
{
    const char *one[] = {icon_name, NULL};
    return make_primary_popover_with_icons(label, one, action, self);
}

/* Flat circular swatches — kill default button padding so the drawing area fills the toggle. */

static void install_theme_swatch_css(void)
{
    static gboolean installed = FALSE;
    if (installed) return;
    installed = TRUE;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        "button.theme-swatch-button,"
        "button.theme-swatch-button:hover,"
        "button.theme-swatch-button:checked,"
        "button.theme-swatch-button:checked:hover {"
        "  padding: 0;"
        "  min-width: 0;"
        "  min-height: 0;"
        "  background: transparent;"
        "  border-color: transparent;"
        "  box-shadow: none;"
        "  outline-color: transparent;"
        "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_theme_selector_clicked(GtkToggleButton *button, gpointer user_data)
{
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    if (!gtk_toggle_button_get_active(button)) return;

    const char *mode = g_object_get_data(G_OBJECT(button), "silktex-theme");
    if (mode != NULL) {
        g_action_group_activate_action(G_ACTION_GROUP(self), "set-theme", g_variant_new_string(mode));
    }
}

static void draw_theme_swatch(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                              gpointer user_data)
{
    (void)area;
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(user_data);
    const char *mode = g_object_get_data(G_OBJECT(button), "silktex-theme");
    gboolean active = gtk_toggle_button_get_active(button);

    double size = MIN(width, height) - 4.0;
    double x = (width - size) / 2.0;
    double y = (height - size) / 2.0;
    double r = size / 2.0;
    double cx = x + r;
    double cy = y + r;

    cairo_save(cr);
    cairo_arc(cr, cx, cy, r - 1.0, 0, 2 * G_PI);
    cairo_clip(cr);

    if (g_strcmp0(mode, "follow") == 0) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_move_to(cr, x + size, y);
        cairo_line_to(cr, x + size, y + size);
        cairo_line_to(cr, x, y + size);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, 0.08, 0.08, 0.09);
        cairo_fill(cr);
    } else if (g_strcmp0(mode, "dark") == 0) {
        cairo_set_source_rgb(cr, 0.16, 0.15, 0.14);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    cairo_restore(cr);

    cairo_arc(cr, cx, cy, r - 1.0, 0, 2 * G_PI);
    if (active) {
        cairo_set_source_rgb(cr, 0.21, 0.52, 0.89);
        cairo_set_line_width(cr, 2.0);
    } else {
        cairo_set_source_rgba(cr, 0.45, 0.45, 0.48, 0.35);
        cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    if (active) {
        double br = 8.5;
        double bx = x + size - br - 1.0;
        double by = y + size - br - 1.0;

        cairo_arc(cr, bx, by, br, 0, 2 * G_PI);
        cairo_set_source_rgb(cr, 0.21, 0.52, 0.89);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, bx - 3.5, by);
        cairo_line_to(cr, bx - 0.8, by + 3.0);
        cairo_line_to(cr, bx + 4.5, by - 4.0);
        cairo_stroke(cr);
    }
}

static void on_theme_swatch_active_changed(GtkToggleButton *button, GParamSpec *pspec,
                                           gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    GtkWidget *child = gtk_button_get_child(GTK_BUTTON(button));
    if (GTK_IS_DRAWING_AREA(child) && gtk_widget_get_width(child) > 0 &&
        gtk_widget_get_height(child) > 0)
        gtk_widget_queue_draw(child);
}

static void on_primary_popover_show(GtkWidget *popover, gpointer user_data)
{
    (void)popover;
    SilktexWindow *self = SILKTEX_WINDOW(user_data);
    update_theme_buttons(self, config_get_string("Interface", "theme"));
}

static GtkWidget *make_theme_toggle(const char *mode, const char *tooltip, SilktexWindow *self)
{
    GtkWidget *button = gtk_toggle_button_new();
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "theme-swatch-button");

    g_object_set_data_full(G_OBJECT(button), "silktex-theme", g_strdup(mode), g_free);

    GtkWidget *swatch = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(swatch), draw_theme_swatch, button, NULL);
    gtk_widget_set_size_request(swatch, 52, 52);
    gtk_widget_set_margin_start(swatch, 4);
    gtk_widget_set_margin_end(swatch, 4);
    gtk_widget_set_margin_top(swatch, 2);
    gtk_widget_set_margin_bottom(swatch, 2);
    gtk_button_set_child(GTK_BUTTON(button), swatch);

    g_signal_connect(button, "clicked", G_CALLBACK(on_theme_selector_clicked), self);
    g_signal_connect(button, "notify::active", G_CALLBACK(on_theme_swatch_active_changed), NULL);
    return button;
}

/*
 * Attaches a GtkPopover to btn_menu. Action names use the GtkApplication
 * prefix (win.* / app.*) so they match entries registered on the window and app.
 */

void silktex_window_install_primary_menu(SilktexWindow *self)
{
    install_theme_swatch_css();

    GtkWidget *popover = gtk_popover_new();
    gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    GtkWidget *theme = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(theme, GTK_ALIGN_CENTER);

    GtkWidget *follow = make_theme_toggle("follow", _("Follow System Theme"), self);
    GtkWidget *light = make_theme_toggle("light", _("Light"), self);
    GtkWidget *dark = make_theme_toggle("dark", _("Dark"), self);
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(light), GTK_TOGGLE_BUTTON(follow));
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(dark), GTK_TOGGLE_BUTTON(follow));

    self->theme_follow = GTK_TOGGLE_BUTTON(follow);
    self->theme_light = GTK_TOGGLE_BUTTON(light);
    self->theme_dark = GTK_TOGGLE_BUTTON(dark);

    gtk_box_append(GTK_BOX(theme), follow);
    gtk_box_append(GTK_BOX(theme), light);
    gtk_box_append(GTK_BOX(theme), dark);
    gtk_box_append(GTK_BOX(box), theme);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_New"), "document-new-symbolic", "win.new", self));
    gtk_box_append(GTK_BOX(box), make_primary_popover_button(_("_Open…"),
                                                             "document-open-symbolic", "win.open",
                                                             self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("Open _Recent…"), "document-open-recent-symbolic",
                                               "win.show-recent", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Fullscreen"), "view-fullscreen-symbolic",
                                               "win.fullscreen", self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Preferences"), "emblem-system-symbolic",
                                               "win.preferences", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Shortcuts"), "preferences-desktop-keyboard-symbolic",
                                               "win.shortcuts", self));
    gtk_box_append(GTK_BOX(box),
                   make_primary_popover_button(_("_Info"), "help-about-symbolic",
                                               "app.about", self));

    gtk_menu_button_set_menu_model(self->btn_menu, NULL);
    gtk_menu_button_set_popover(self->btn_menu, popover);
    g_signal_connect(popover, "show", G_CALLBACK(on_primary_popover_show), self);
}

static void action_open_menu(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    (void)p;
    SilktexWindow *self = SILKTEX_WINDOW(ud);
    if (self->btn_menu) gtk_menu_button_popup(self->btn_menu);
}

static const GActionEntry menu_actions[] = {
    {"set-theme", NULL, "s", "'follow'", change_theme},
    {"show-recent", action_show_recent},
    {"shortcuts", action_shortcuts},
    {"open-menu", action_open_menu},
};

void silktex_window_register_menu_actions(SilktexWindow *self)
{
    g_action_map_add_action_entries(G_ACTION_MAP(self), menu_actions, G_N_ELEMENTS(menu_actions),
                                    self);
}
