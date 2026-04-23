/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gtksourceview/gtksource.h>
#include <adwaita.h>

G_BEGIN_DECLS

#define SILKTEX_TYPE_EDITOR (silktex_editor_get_type())
G_DECLARE_FINAL_TYPE(SilktexEditor, silktex_editor, SILKTEX, EDITOR, GObject)

SilktexEditor *silktex_editor_new(void);

GtkWidget *silktex_editor_get_view(SilktexEditor *self);
GtkSourceBuffer *silktex_editor_get_buffer(SilktexEditor *self);

void silktex_editor_set_filename(SilktexEditor *self, const char *filename);
const char *silktex_editor_get_filename(SilktexEditor *self);
char *silktex_editor_get_basename(SilktexEditor *self);

void silktex_editor_load_file(SilktexEditor *self, GFile *file);
gboolean silktex_editor_save_file(SilktexEditor *self, GFile *file, GError **error);

void silktex_editor_set_text(SilktexEditor *self, const char *text, gssize len);
char *silktex_editor_get_text(SilktexEditor *self);

gboolean silktex_editor_get_modified(SilktexEditor *self);
void silktex_editor_set_modified(SilktexEditor *self, gboolean modified);

void silktex_editor_undo(SilktexEditor *self);
void silktex_editor_redo(SilktexEditor *self);
gboolean silktex_editor_can_undo(SilktexEditor *self);
gboolean silktex_editor_can_redo(SilktexEditor *self);

void silktex_editor_set_style_scheme(SilktexEditor *self, const char *scheme_id);
void silktex_editor_set_font(SilktexEditor *self, const char *font_desc);

void silktex_editor_scroll_to_line(SilktexEditor *self, int line);
void silktex_editor_scroll_to_cursor(SilktexEditor *self);

/* Navigate by line (0-based) */
void silktex_editor_goto_line(SilktexEditor *self, int line);
int  silktex_editor_get_cursor_line(SilktexEditor *self);

/* Apply all editor settings from configfile */
void silktex_editor_apply_settings(SilktexEditor *self);

void silktex_editor_apply_textstyle(SilktexEditor *self, const char *style_type);

void silktex_editor_insert_package(SilktexEditor *self, const char *package, const char *options);

void silktex_editor_search(SilktexEditor *self, const char *term, gboolean backwards,
                           gboolean whole_word, gboolean match_case);
void silktex_editor_search_next(SilktexEditor *self, gboolean backwards);
void silktex_editor_replace(SilktexEditor *self, const char *term, const char *replacement,
                            gboolean backwards, gboolean whole_word, gboolean match_case);
void silktex_editor_replace_all(SilktexEditor *self, const char *term, const char *replacement,
                                 gboolean whole_word, gboolean match_case);

const char *silktex_editor_get_workfile(SilktexEditor *self);
const char *silktex_editor_get_pdffile(SilktexEditor *self);
void silktex_editor_update_workfile(SilktexEditor *self);

G_END_DECLS
