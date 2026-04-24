/*
 * SilkTex - utils.c
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>
#include <unistd.h>

#ifdef WIN32
    #include <windows.h>
#else
    #include <sys/types.h>
    #include <sys/wait.h>
#endif

#include "utils.h"

static gchar *slogmsg_info = "\e[1;34m[Info]\e[0m ";
static gchar *slogmsg_debug = "\e[1;32m[Debug]\e[0m ";
static gchar *slogmsg_error = "\e[1;31m[Error]\e[0m ";
static gchar *slogmsg_warning = "\e[1;33m[Warning]\e[0m ";
static gchar *slogmsg_fatal = "\e[1;37;41m[Fatal]\e[0m ";

static gint slog_debug = 0;
GThread *main_thread = NULL;

void slog_init(gint debug) {
    slog_debug = debug;
    main_thread = g_thread_self();
}

gboolean in_debug_mode(void) {
    return slog_debug;
}

void slog(gint level, const gchar *fmt, ...) {
    gchar message[BUFSIZ];
    va_list vap;

    if (L_IS_TYPE(level, L_DEBUG) && !slog_debug) return;

    if (L_IS_TYPE(level, L_DEBUG))
        g_fprintf(stderr, "%s", slogmsg_debug);
    else if (L_IS_TYPE(level, L_FATAL))
        g_fprintf(stderr, "%s", slogmsg_fatal);
    else if (L_IS_TYPE(level, L_ERROR))
        g_fprintf(stderr, "%s", slogmsg_error);
    else if (L_IS_TYPE(level, L_WARNING))
        g_fprintf(stderr, "%s", slogmsg_warning);
    else
        g_fprintf(stderr, "%s", slogmsg_info);

    va_start(vap, fmt);
    vsnprintf(message, BUFSIZ, fmt, vap);
    va_end(vap);
    fprintf(stderr, "%s", message);

    if (L_IS_TYPE(level, L_FATAL)) {
        exit(1);
    }
}

gboolean utils_path_exists(const gchar *path) {
    if (path == NULL) return FALSE;
    return g_file_test(path, G_FILE_TEST_EXISTS);
}

gboolean utils_copy_file(const gchar *source, const gchar *dest, GError **err) {
    gchar *contents;
    gsize length;

    g_return_val_if_fail(source != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

    if (!g_file_get_contents(source, &contents, &length, err))
        return FALSE;

    if (!g_file_set_contents(dest, contents, length, err)) {
        g_free(contents);
        return FALSE;
    }

    g_free(contents);
    return TRUE;
}

gboolean utils_subinstr(const gchar *substr, const gchar *target, gboolean case_insens) {
    if (target == NULL || substr == NULL) return FALSE;

    if (case_insens) {
        g_autofree gchar *ntarget = g_utf8_strup(target, -1);
        g_autofree gchar *nsubstr = g_utf8_strup(substr, -1);
        return g_strstr_len(ntarget, -1, nsubstr) != NULL;
    }
    return g_strstr_len(target, -1, substr) != NULL;
}

gchar *g_substr(gchar *src, gint start, gint end) {
    gint len = end - start + 1;
    gchar *dst = g_malloc0(len);
    return strncpy(dst, &src[start], end - start);
}

slist *slist_find(slist *head, const gchar *term, gboolean n, gboolean create) {
    slist *current = head;
    slist *prev = NULL;

    while (current) {
        if (n) {
            if (strncmp(current->first, term, strlen(term)) == 0)
                return current;
        } else {
            if (g_strcmp0(current->first, term) == 0)
                return current;
        }
        prev = current;
        current = current->next;
    }

    if (create && prev != NULL) {
        prev->next = g_new0(slist, 1);
        current = prev->next;
        current->first = g_strdup(term);
        current->second = g_strdup("");
    }
    return current;
}

slist *slist_append(slist *head, slist *node) {
    slist *current = head;
    slist *prev = NULL;

    while (current) {
        prev = current;
        current = current->next;
    }
    if (prev != NULL) {
        prev->next = node;
    }
    return head;
}

slist *slist_remove(slist *head, slist *node) {
    slist *current = head;
    slist *prev = NULL;

    while (current) {
        if (current == node) break;
        prev = current;
        current = current->next;
    }
    if (current) {
        if (current == head)
            head = head->next;
        else if (prev != NULL)
            prev->next = current->next;
    }
    return head;
}
