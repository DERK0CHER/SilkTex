/*
 * SilkTex - utils.h
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared macros (log levels, path permissions), slog_* logging, slist, and
 * small string/file helpers used across the codebase.
 */

#ifndef SILKTEX_UTILS_H
#define SILKTEX_UTILS_H

#include <glib.h>

#ifdef WIN32
#define DIR_PERMS (S_IRWXU)
#else
#define DIR_PERMS (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#endif

#define STR_EQU(X, Y) (g_strcmp0((X), (Y)) == 0)

#define L_IS_TYPE(level, type) ((level & type) == type)
#define L_INFO                 0x00
#define L_WARNING              0x01
#define L_DEBUG                0x02
#define L_ERROR                0x04
#define L_FATAL                0x08

typedef struct _slist {
    struct _slist *next;
    gchar *first;
    gchar *second;
} slist;

void slog_init(gint debug);
gboolean in_debug_mode(void);
void slog(gint level, const gchar *fmt, ...);

gboolean utils_path_exists(const gchar *path);
gboolean utils_copy_file(const gchar *source, const gchar *dest, GError **err);
gboolean utils_subinstr(const gchar *substr, const gchar *target, gboolean case_insens);
gchar *g_substr(gchar *src, gint start, gint end);

slist *slist_find(slist *head, const gchar *term, gboolean n, gboolean create);
slist *slist_append(slist *head, slist *node);
slist *slist_remove(slist *head, slist *node);

#endif /* SILKTEX_UTILS_H */
