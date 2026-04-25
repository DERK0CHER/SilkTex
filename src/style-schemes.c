/*
 * SilkTex — bundled GtkSourceView style schemes and resolution.
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Implements path registration and "auto" theme: picks Gruvbox or Lights-out
 * variants based on AdwStyleManager and config overrides. GUMMI_DATA is the
 * compile-time data root (historical macro name from the Gummi fork).
 */

#include "style-schemes.h"
#include "configfile.h"
#include "constants.h"

#include <adwaita.h>
#include <gtksourceview/gtksource.h>
#include <string.h>

#ifndef GUMMI_DATA
#define GUMMI_DATA "/usr/share/silktex"
#endif

static gboolean paths_initialized;

/* Append $sharedata/silktex/styles/ so bundled schemes (Gruvbox, Lights out) are found. */
void silktex_init_style_scheme_paths(void)
{
    if (paths_initialized) return;
    paths_initialized = TRUE;

    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();

    g_autofree char *dev = g_build_filename(GUMMI_DATA, "styles", NULL);
    if (g_file_test(dev, G_FILE_TEST_IS_DIR))
        gtk_source_style_scheme_manager_prepend_search_path(mgr, dev);

    const char *const *dirs = g_get_system_data_dirs();
    for (int i = 0; dirs && dirs[i]; i++) {
        g_autofree char *p = g_build_filename(dirs[i], "silktex", "styles", NULL);
        if (g_file_test(p, G_FILE_TEST_IS_DIR))
            gtk_source_style_scheme_manager_prepend_search_path(mgr, p);
    }

    g_autofree char *confdir = C_GUMMI_CONFDIR;
    g_autofree char *custom = g_build_filename(confdir, "styles", NULL);
    if (g_file_test(custom, G_FILE_TEST_IS_DIR))
        gtk_source_style_scheme_manager_prepend_search_path(mgr, custom);
}

/* Map window theme mode → default editor scheme id. */
static const char *default_scheme_for_ui_mode(void)
{
    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();
    const char *t = config_get_string("Interface", "theme");
    const char *light = config_get_string("Editor", "style_scheme_light");
    const char *dark = config_get_string("Editor", "style_scheme_dark");
    if (!light || !*light) light = "silktex-gruvbox-light";
    if (!dark || !*dark) dark = "silktex-gruvbox-dark";
    if (gtk_source_style_scheme_manager_get_scheme(mgr, light) == NULL)
        light = "silktex-gruvbox-light";
    if (gtk_source_style_scheme_manager_get_scheme(mgr, dark) == NULL)
        dark = "silktex-gruvbox-dark";
    if (g_strcmp0(t, "dark") == 0) return dark;
    if (g_strcmp0(t, "light") == 0) return light;
    if (g_strcmp0(t, "follow") == 0) {
        if (adw_style_manager_get_dark(adw_style_manager_get_default())) return dark;
        return light;
    }
    /* Unknown mode — treat like follow. */
    if (adw_style_manager_get_dark(adw_style_manager_get_default())) return dark;
    return light;
}

/* First installed scheme in try_ids, or NULL. */
static const char *first_existing_scheme(GtkSourceStyleSchemeManager *mgr, const char *const *try_ids)
{
    for (int i = 0; try_ids[i]; i++) {
        if (gtk_source_style_scheme_manager_get_scheme(mgr, try_ids[i]) != NULL) return try_ids[i];
    }
    return NULL;
}

const char *silktex_resolved_style_scheme_id(void)
{
    silktex_init_style_scheme_paths();

    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();

    const char *d = default_scheme_for_ui_mode();
    if (gtk_source_style_scheme_manager_get_scheme(mgr, d) != NULL) return d;

    const char *const fallbacks[] = {
        "silktex-gruvbox-dark", "silktex-gruvbox-light", "silktex-lights-out", "Adwaita-dark",
        "Adwaita",              "classic",              NULL,
    };
    const char *f = first_existing_scheme(mgr, fallbacks);
    return f ? f : d;
}
