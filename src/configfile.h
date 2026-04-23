/*
 * SilkTex - configfile.h
 * SPDX-License-Identifier: MIT
 */

#ifndef SILKTEX_CONFIGFILE_H
#define SILKTEX_CONFIGFILE_H

#include <glib.h>

void config_init(void);
void config_save(void);

const gchar *config_get_string(const gchar *group, const gchar *key);
gboolean config_get_boolean(const gchar *group, const gchar *key);
gint config_get_integer(const gchar *group, const gchar *key);

void config_set_string(const gchar *group, const gchar *key, const gchar *value);
void config_set_boolean(const gchar *group, const gchar *key, gboolean value);
void config_set_integer(const gchar *group, const gchar *key, gint value);

#endif /* SILKTEX_CONFIGFILE_H */
