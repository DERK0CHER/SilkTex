/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "application.h"
#include "window.h"
#include "configfile.h"
#include "utils.h"
#include <glib/gi18n.h>

struct _SilktexApplication {
    AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (SilktexApplication, silktex_application, ADW_TYPE_APPLICATION)

    static void action_quit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
    {
        GApplication *app = G_APPLICATION(user_data);
        g_application_quit(app);
    }

static void action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *window = gtk_application_get_active_window(app);

    const char *developers[] = {"Bela Georg Barthelmes", NULL};

    adw_show_about_dialog(GTK_WIDGET(window), "application-name", "SilkTex", "application-icon",
                          "app.silktex.SilkTex", "version", "0.9.0", "copyright",
                          "© 2026 Bela Georg Barthelmes", "license-type", GTK_LICENSE_GPL_3_0,
                          "comments", _("A modern LaTeX editor for GNOME"), "website",
                          "https://github.com/DERK0CHER/SilkTex", "developers", developers,
                          "translator-credits", _("translator-credits"), NULL);
}

static const GActionEntry app_actions[] = {
    {"quit", action_quit},
    {"about", action_about},
};

static void silktex_application_activate(GApplication *app)
{
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));

    if (window == NULL) {
        window = GTK_WINDOW(silktex_window_new(ADW_APPLICATION(app)));
    }

    gtk_window_present(window);
}

static void silktex_application_open(GApplication *app, GFile **files, int n_files,
                                     const char *hint)
{
    SilktexWindow *window;

    window = SILKTEX_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(app)));
    if (window == NULL) {
        window = silktex_window_new(ADW_APPLICATION(app));
    }

    for (int i = 0; i < n_files; i++) {
        silktex_window_open_file(window, files[i]);
    }

    gtk_window_present(GTK_WINDOW(window));
}

static void silktex_application_startup(GApplication *app)
{
    G_APPLICATION_CLASS(silktex_application_parent_class)->startup(app);

    slog_init(0);
    config_init();

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

    const char *quit_accels[] = {"<Control>q", NULL};
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quit_accels);
}

static void silktex_application_init(SilktexApplication *self) {}

static void silktex_application_class_init(SilktexApplicationClass *klass)
{
    GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

    app_class->activate = silktex_application_activate;
    app_class->open = silktex_application_open;
    app_class->startup = silktex_application_startup;
}

SilktexApplication *silktex_application_new(void)
{
    return g_object_new(SILKTEX_TYPE_APPLICATION, "application-id", "app.silktex.SilkTex", "flags",
                        G_APPLICATION_HANDLES_OPEN, "resource-base-path", "/app/silktex", NULL);
}
