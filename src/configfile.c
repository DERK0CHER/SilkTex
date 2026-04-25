/*
 * SilkTex - configfile.c
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GLib KeyFile persistence for ~/.config/silktex/config.ini (see constants.h).
 * config_init() creates the file from an embedded default if missing;
 * getters/setters are thin wrappers used across the app and prefs dialog.
 */

#include "configfile.h"
#include "constants.h"
#include "utils.h"
#include <string.h>
#include <sys/stat.h>

static const gchar default_config[] =
    "[General]\n"
    "config_version = " C_PACKAGE_VERSION "\n"
    "\n"
    "[Interface]\n"
    "mainwindow_w = 1200\n"
    "mainwindow_h = 800\n"
    "mainwindow_max = false\n"
    "# Application theme: follow, light, dark, or lightsout (AMOLED-style dark UI).\n"
    "# 'follow' mirrors the desktop's current preference (GNOME dark style).\n"
    "theme = follow\n"
    "\n"
    "[Editor]\n"
    "# auto = Color scheme follows the theme above (Gruvbox / Lights out).\n"
    "# Or set a GtkSourceView scheme id from Preferences → Color scheme.\n"
    "style_scheme = auto\n"
    "font = Monospace 14\n"
    "line_numbers = true\n"
    "highlighting = true\n"
    "textwrapping = true\n"
    "wordwrapping = true\n"
    "tabwidth = 4\n"
    "spaces_instof_tabs = false\n"
    "autoindentation = true\n"
    "\n"
    "[Preview]\n"
    "zoom = 1.0\n"
    "autosync = false\n"
    "\n"
    "[Compile]\n"
    "typesetter = pdflatex\n"
    "auto_compile = true\n"
    "timer = 1\n"
    "shellescape = false\n"
    "synctex = false\n"
    "\n"
    "[File]\n"
    "autosaving = false\n"
    "autosave_timer = 10\n"
    "autoexport = false\n"
    "\n"
    "[Spelling]\n"
    "enabled = false\n"
    "language = en_US\n"
    "\n"
    "[Snippets]\n"
    "# Two modifier keys combined with each snippet's single-letter accelerator\n"
    "# to form its shortcut. Valid values: Shift, Control, Alt, Super.\n"
    "# Leave empty to disable a modifier slot (e.g. only one modifier).\n"
    "modifier1 = Shift\n"
    "modifier2 = Alt\n";

static GKeyFile *key_file = NULL;
static gchar *conf_filepath = NULL;

void config_init(void)
{
    gchar *confdir = C_GUMMI_CONFDIR;

    if (!g_file_test(confdir, G_FILE_TEST_IS_DIR)) {
        g_mkdir_with_parents(confdir, DIR_PERMS);
    }

    conf_filepath = g_build_filename(confdir, "silktex.ini", NULL);
    g_free(confdir);

    key_file = g_key_file_new();

    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, conf_filepath, G_KEY_FILE_NONE, &error)) {
        if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            slog(L_WARNING, "Config file not found, using defaults\n");
        }
        g_clear_error(&error);

        g_key_file_load_from_data(key_file, default_config, strlen(default_config), G_KEY_FILE_NONE,
                                  NULL);
        config_save();
    }

    /* Migrate older config files that predate the Snippets section. */
    if (!g_key_file_has_group(key_file, "Snippets")) {
        g_key_file_set_string(key_file, "Snippets", "modifier1", "Shift");
        g_key_file_set_string(key_file, "Snippets", "modifier2", "Alt");
        config_save();
    }

    /* Migrate older configs that predate the Interface/theme key. */
    if (!g_key_file_has_key(key_file, "Interface", "theme", NULL)) {
        g_key_file_set_string(key_file, "Interface", "theme", "follow");
        config_save();
    }

    /* Editor colour scheme: prefer "auto" (follows Interface/theme). */
    if (g_key_file_has_key(key_file, "Editor", "style_scheme", NULL)) {
        g_autofree char *ss = g_key_file_get_string(key_file, "Editor", "style_scheme", NULL);
        if (ss && (g_strcmp0(ss, "Adwaita") == 0 || g_strcmp0(ss, "Adwaita-dark") == 0)) {
            g_key_file_set_string(key_file, "Editor", "style_scheme", "auto");
            config_save();
        }
    }

    slog(L_INFO, "Configuration file: %s\n", conf_filepath);
}

void config_save(void)
{
    if (key_file == NULL || conf_filepath == NULL) return;

    GError *error = NULL;
    if (!g_key_file_save_to_file(key_file, conf_filepath, &error)) {
        slog(L_ERROR, "Failed to save config: %s\n", error->message);
        g_error_free(error);
    }
}

const gchar *config_get_string(const gchar *group, const gchar *key)
{
    if (key_file == NULL) return "";

    GError *error = NULL;
    gchar *value = g_key_file_get_string(key_file, group, key, &error);

    if (error != NULL) {
        g_clear_error(&error);
        return "";
    }
    return value;
}

gboolean config_get_boolean(const gchar *group, const gchar *key)
{
    if (key_file == NULL) return FALSE;

    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(key_file, group, key, &error);

    if (error != NULL) {
        g_clear_error(&error);
        return FALSE;
    }
    return value;
}

gint config_get_integer(const gchar *group, const gchar *key)
{
    if (key_file == NULL) return 0;

    GError *error = NULL;
    gint value = g_key_file_get_integer(key_file, group, key, &error);

    if (error != NULL) {
        g_clear_error(&error);
        return 0;
    }
    return value;
}

void config_set_string(const gchar *group, const gchar *key, const gchar *value)
{
    if (key_file == NULL) return;
    g_key_file_set_string(key_file, group, key, value);
}

void config_set_boolean(const gchar *group, const gchar *key, gboolean value)
{
    if (key_file == NULL) return;
    g_key_file_set_boolean(key_file, group, key, value);
}

void config_set_integer(const gchar *group, const gchar *key, gint value)
{
    if (key_file == NULL) return;
    g_key_file_set_integer(key_file, group, key, value);
}
