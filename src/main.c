/**
 * @file   gummi.c
 * @brief
 *
 * Copyright (C) 2009 Gummi Developers
 * All Rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "biblio.h"
#include "configfile.h"
#include "constants.h"
#include "environment.h"
#include "external.h"
#include "gui/gui-main.h"
#include "iofunctions.h"
#include "motion.h"
#include "project.h"
#include "signals.h"
#include "snippets.h"
#include "tabmanager.h"
#include "template.h"
#include "utils.h"

extern Gummi* gummi;
extern GummiGui* gui;
static int debug = 0;
static int showversion = 0;
static GMainLoop* shell_loop = NULL;

static GOptionEntry entries[] = {
    { (const gchar*)"debug", (gchar)'d', 0, G_OPTION_ARG_NONE,
        &debug, (gchar*)"show debug info", NULL},
    { (const gchar*)"version", (gchar)'v', 0, G_OPTION_ARG_NONE,
        &showversion, (gchar*)"show version and exit", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
};

static gboolean
on_shell_window_close_request (GtkWindow* window, gpointer user_data) {
    (void)window;
    (void)user_data;
    if (shell_loop != NULL) {
        g_main_loop_quit (shell_loop);
    }
    return FALSE;
}

int main (int argc, char *argv[]) {
    /* set up i18n */
    bindtextdomain (C_PACKAGE, GUMMI_LOCALES);
    setlocale (LC_ALL, "");
    textdomain (C_PACKAGE);

    GError* error = NULL;
    GOptionContext* context = g_option_context_new ("files");
    g_option_context_add_main_entries (context, entries, C_PACKAGE);
    g_option_context_parse (context, &argc, &argv, &error);
    if (error) g_error("%s\n", error->message);

    if (showversion) {
        printf("Gummi %s\n", C_PACKAGE_VERSION);
        return 0;
    }

    // Initialize GTK
    gtk_init ();
    adw_init ();

    GError* ui_error = NULL;
    GtkBuilder* builder = gtk_builder_new ();
    gchar* ui = g_build_filename (GUMMI_DATA, "ui", "silktex.ui", NULL);

    // Exit program when UI file can not be located:
    if (!g_file_test (ui, G_FILE_TEST_EXISTS)) {
        printf("Could not locate interface file at:\n%s\n", ui);
        return 0;
    }

    gtk_builder_add_from_file (builder, ui, &ui_error);
    if (ui_error) {
        g_error ("%s\n", ui_error->message);
    }
    gtk_builder_set_translation_domain (builder, C_PACKAGE);
    g_free (ui);

    /* Bootable GTK4 shell path:
     * If we have the new Adwaita shell but not the legacy widget tree, present
     * the shell window and run a plain GLib loop until close-request. */
    GtkWindow* shell_window =
        GTK_WINDOW (gtk_builder_get_object (builder, "main-window"));
    GtkWindow* legacy_window =
        GTK_WINDOW (gtk_builder_get_object (builder, "mainwindow"));
    if (shell_window != NULL && legacy_window == NULL) {
        g_signal_connect (shell_window, "close-request",
                          G_CALLBACK (on_shell_window_close_request), NULL);
        gtk_window_present (shell_window);
        shell_loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (shell_loop);
        g_main_loop_unref (shell_loop);
        shell_loop = NULL;
        return 0;
    }

    /* Initialize logging */
    slog_init (debug);
    slog (L_INFO, C_PACKAGE_NAME" version: "C_PACKAGE_VERSION"\n");

    // Initialize configuration
    config_init ();

    /* Initialize signals */
    gummi_signals_register ();

    /* Initialize Classes */
    // why do we not load this from environment, like gui-main does? -A
    GuMotion* motion = motion_init ();
    GuIOFunc* io = iofunctions_init();
    GuLatex* latex = latex_init ();
    GuBiblio* biblio = biblio_init (builder);
    GuTemplate* templ = template_init (builder);
    GuTabmanager* tabm = tabmanager_init ();
    GuProject* proj = project_init ();
    GuSnippets* snippets = snippets_init ();

    gummi = gummi_init (motion, io, latex, biblio, templ, snippets, tabm, proj);
    slog (L_DEBUG, "Gummi created!\n");

    /* Initialize GUI */
    gui = gui_init (builder);

    slog_set_gui_parent (gui->mainwindow);
    slog (L_DEBUG, "GummiGui created!\n");

    /* Start compile thread */
    if (external_exists (config_get_string ("Compile", "typesetter"))) {
        typesetter_setup ();
        motion_start_compile_thread (motion);
    }
    else {
        infoscreengui_enable (gui->infoscreengui, "program_error");
        slog (L_ERROR, "Could not locate the typesetter program\n");
    }

    /* Install acceleration group to mainwindow */
    gtk_window_add_accel_group (gui->mainwindow, snippets->accel_group);

    // tab/file loading:
    if (argc < 2) {
        tabmanager_create_tab (A_DEFAULT, NULL, NULL);
    }
    else {
        int i;
        for (i = 1; i < argc; i++) {
            if (!g_file_test(argv[i], G_FILE_TEST_EXISTS)) {
                slog(L_ERROR, "Failed to open file '%s': "
                              "No such file or directory\n", argv[i]);
                exit(1);
            }
            tabmanager_create_tab (A_LOAD, argv[i], NULL);
        }
    }

    if (config_get_boolean ("File", "autosaving")) {
        iofunctions_start_autosave ();
    }

    gui_main (builder);
    config_save ();
    return 0;
}
