/*
 * SilkTex - constants.h
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SILKTEX_CONSTANTS_H
#define SILKTEX_CONSTANTS_H

#include <glib.h>

#define C_PACKAGE         "silktex"
#define C_PACKAGE_NAME    "SilkTex"
#define C_PACKAGE_VERSION "0.9.0"

#define C_GUMMI_CONFDIR     g_build_path(G_DIR_SEPARATOR_S, g_get_user_config_dir(), "silktex", NULL)
#define C_GUMMI_TEMPLATEDIR g_build_path(G_DIR_SEPARATOR_S, C_GUMMI_CONFDIR, "templates", NULL)

#ifdef WIN32
#define C_TMPDIR g_build_path(G_DIR_SEPARATOR_S, g_get_home_dir(), "silktex_tmp", NULL)
#else
#define C_TMPDIR g_build_path(G_DIR_SEPARATOR_S, g_get_user_cache_dir(), "silktex", NULL)
#endif

#define C_DIRSEP G_DIR_SEPARATOR_S

#endif /* SILKTEX_CONSTANTS_H */
