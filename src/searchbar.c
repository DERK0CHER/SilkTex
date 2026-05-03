/*
 * SilkTex - Inline find/replace bar
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "searchbar.h"
#include "i18n.h"

struct _SilktexSearchbar {
    GtkWidget parent_instance;

    /* Logical open state — gtk_revealer_get_reveal_child can disagree briefly during
     * transitions and embedding in toolbar views, which broke toolbar toggle-close. */
    gboolean revealed;

    GtkWidget *revealer;
    GtkWidget *search_entry;
    GtkWidget *replace_entry;
    GtkWidget *replace_row; /* hbox containing replace widgets */
    GtkWidget *btn_prev;
    GtkWidget *btn_next;
    GtkWidget *btn_replace;
    GtkWidget *btn_replace_all;
    GtkCheckButton *chk_case;
    GtkCheckButton *chk_whole;
    GtkCheckButton *chk_backwards;

    SilktexEditor *editor;
};

G_DEFINE_FINAL_TYPE (SilktexSearchbar, silktex_searchbar, GTK_TYPE_WIDGET)

    static void do_search(SilktexSearchbar *self)
    {
        if (!self->editor) return;
        const char *term = gtk_editable_get_text(GTK_EDITABLE(self->search_entry));
        if (!term || !*term) return;

        gboolean backwards = gtk_check_button_get_active(self->chk_backwards);
        gboolean whole = gtk_check_button_get_active(self->chk_whole);
        gboolean matchcase = gtk_check_button_get_active(self->chk_case);

        silktex_editor_search(self->editor, term, backwards, whole, matchcase);
    }

static void on_search_changed(GtkEditable *e, gpointer ud)
{
    do_search(SILKTEX_SEARCHBAR(ud));
}

static void on_search_opt_changed(GObject *obj, GParamSpec *pspec, gpointer ud)
{
    (void)obj;
    (void)pspec;
    do_search(SILKTEX_SEARCHBAR(ud));
}

static void on_search_activate(GtkEntry *e, gpointer ud)
{
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    (void)e;
    if (!self->editor) return;
    silktex_editor_search_next(self->editor, gtk_check_button_get_active(self->chk_backwards));
}

static gboolean on_search_entry_key_capture(GtkEventControllerKey *ctrl, guint keyval,
                                             guint keycode, GdkModifierType state, gpointer ud)
{
    (void)ctrl; (void)keycode; (void)state;
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    if (keyval == GDK_KEY_Escape) {
        silktex_searchbar_close(self);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean on_replace_entry_key_capture(GtkEventControllerKey *ctrl, guint keyval,
                                               guint keycode, GdkModifierType state, gpointer ud)
{
    (void)ctrl;
    (void)keycode;
    (void)state;
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    if (keyval == GDK_KEY_Escape) {
        silktex_searchbar_close(self);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void on_btn_prev(GtkButton *b, gpointer ud)
{
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    if (!self->editor) return;
    silktex_editor_search_next(self->editor, TRUE);
}

static void on_btn_next(GtkButton *b, gpointer ud)
{
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    if (!self->editor) return;
    silktex_editor_search_next(self->editor, FALSE);
}

static void on_btn_replace(GtkButton *b, gpointer ud)
{
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    if (!self->editor) return;
    const char *term = gtk_editable_get_text(GTK_EDITABLE(self->search_entry));
    const char *repl = gtk_editable_get_text(GTK_EDITABLE(self->replace_entry));
    gboolean backwards = gtk_check_button_get_active(self->chk_backwards);
    gboolean whole = gtk_check_button_get_active(self->chk_whole);
    gboolean matchcase = gtk_check_button_get_active(self->chk_case);
    silktex_editor_replace(self->editor, term, repl, backwards, whole, matchcase);
}

static void on_btn_replace_all(GtkButton *b, gpointer ud)
{
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(ud);
    if (!self->editor) return;
    const char *term = gtk_editable_get_text(GTK_EDITABLE(self->search_entry));
    const char *repl = gtk_editable_get_text(GTK_EDITABLE(self->replace_entry));
    gboolean whole = gtk_check_button_get_active(self->chk_whole);
    gboolean matchcase = gtk_check_button_get_active(self->chk_case);
    silktex_editor_replace_all(self->editor, term, repl, whole, matchcase);
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                               GdkModifierType state, gpointer ud)
{
    if (keyval == GDK_KEY_Escape) {
        silktex_searchbar_close(SILKTEX_SEARCHBAR(ud));
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void silktex_searchbar_dispose(GObject *obj)
{
    SilktexSearchbar *self = SILKTEX_SEARCHBAR(obj);
    g_clear_object(&self->editor);
    gtk_widget_unparent(self->revealer);
    G_OBJECT_CLASS(silktex_searchbar_parent_class)->dispose(obj);
}

static void silktex_searchbar_class_init(SilktexSearchbarClass *klass)
{
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->dispose = silktex_searchbar_dispose;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(klass), GTK_TYPE_BIN_LAYOUT);
}

static void silktex_searchbar_init(SilktexSearchbar *self)
{
    self->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(self->revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_widget_set_parent(self->revealer, GTK_WIDGET(self));

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(vbox, 4);
    gtk_widget_set_margin_bottom(vbox, 4);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_revealer_set_child(GTK_REVEALER(self->revealer), vbox);

    GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    self->search_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(self->search_entry, TRUE);
    gtk_widget_set_tooltip_text(self->search_entry, _("Search term"));
    g_signal_connect(self->search_entry, "changed", G_CALLBACK(on_search_changed), self);
    g_signal_connect(self->search_entry, "activate", G_CALLBACK(on_search_activate), self);

    GtkEventControllerKey *entry_key_ctrl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(entry_key_ctrl), GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(self->search_entry, GTK_EVENT_CONTROLLER(entry_key_ctrl));
    g_signal_connect(entry_key_ctrl, "key-pressed", G_CALLBACK(on_search_entry_key_capture), self);

    self->btn_prev = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_set_tooltip_text(self->btn_prev, _("Previous match"));
    g_signal_connect(self->btn_prev, "clicked", G_CALLBACK(on_btn_prev), self);

    self->btn_next = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_set_tooltip_text(self->btn_next, _("Next match"));
    g_signal_connect(self->btn_next, "clicked", G_CALLBACK(on_btn_next), self);

    GtkWidget *btn_close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(btn_close, _("Close"));
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(silktex_searchbar_close), self);

    gtk_box_append(GTK_BOX(search_row), self->search_entry);
    gtk_box_append(GTK_BOX(search_row), self->btn_prev);
    gtk_box_append(GTK_BOX(search_row), self->btn_next);
    gtk_box_append(GTK_BOX(search_row), btn_close);

    self->replace_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_visible(self->replace_row, FALSE);

    self->replace_entry = gtk_entry_new();
    gtk_widget_set_hexpand(self->replace_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->replace_entry), _("Replacement"));

    self->btn_replace = gtk_button_new_with_label(_("Replace"));
    g_signal_connect(self->btn_replace, "clicked", G_CALLBACK(on_btn_replace), self);

    self->btn_replace_all = gtk_button_new_with_label(_("Replace All"));
    g_signal_connect(self->btn_replace_all, "clicked", G_CALLBACK(on_btn_replace_all), self);

    gtk_box_append(GTK_BOX(self->replace_row), self->replace_entry);
    gtk_box_append(GTK_BOX(self->replace_row), self->btn_replace);
    gtk_box_append(GTK_BOX(self->replace_row), self->btn_replace_all);

    GtkEventControllerKey *repl_key_ctrl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(repl_key_ctrl), GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(self->replace_entry, GTK_EVENT_CONTROLLER(repl_key_ctrl));
    g_signal_connect(repl_key_ctrl, "key-pressed", G_CALLBACK(on_replace_entry_key_capture), self);

    GtkWidget *opts_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    self->chk_case = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Match Case")));
    self->chk_whole = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Whole Word")));
    self->chk_backwards = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("Backwards")));

    g_signal_connect(self->chk_case, "notify::active", G_CALLBACK(on_search_opt_changed), self);
    g_signal_connect(self->chk_whole, "notify::active", G_CALLBACK(on_search_opt_changed), self);
    g_signal_connect(self->chk_backwards, "notify::active", G_CALLBACK(on_search_opt_changed), self);

    gtk_box_append(GTK_BOX(opts_row), GTK_WIDGET(self->chk_case));
    gtk_box_append(GTK_BOX(opts_row), GTK_WIDGET(self->chk_whole));
    gtk_box_append(GTK_BOX(opts_row), GTK_WIDGET(self->chk_backwards));

    gtk_box_append(GTK_BOX(vbox), search_row);
    gtk_box_append(GTK_BOX(vbox), self->replace_row);
    gtk_box_append(GTK_BOX(vbox), opts_row);

    GtkEventControllerKey *key_ctrl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(key_ctrl));
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_key_pressed), self);
}

SilktexSearchbar *silktex_searchbar_new(void)
{
    return g_object_new(SILKTEX_TYPE_SEARCHBAR, NULL);
}

gboolean silktex_searchbar_is_open(SilktexSearchbar *self)
{
    g_return_val_if_fail(SILKTEX_IS_SEARCHBAR(self), FALSE);
    return self->revealed;
}

void silktex_searchbar_open(SilktexSearchbar *self, gboolean replace_mode)
{
    g_return_if_fail(SILKTEX_IS_SEARCHBAR(self));
    self->revealed = TRUE;
    gtk_widget_set_visible(self->replace_row, replace_mode);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->revealer), TRUE);
    gtk_widget_grab_focus(self->search_entry);
}

void silktex_searchbar_close(SilktexSearchbar *self)
{
    g_return_if_fail(SILKTEX_IS_SEARCHBAR(self));
    self->revealed = FALSE;
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->revealer), FALSE);
    if (self->editor) gtk_widget_grab_focus(silktex_editor_get_view(self->editor));
}

void silktex_searchbar_set_editor(SilktexSearchbar *self, SilktexEditor *editor)
{
    g_return_if_fail(SILKTEX_IS_SEARCHBAR(self));
    g_set_object(&self->editor, editor);
}
