/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#include "silktex-editor.h"
#include "configfile.h"
#include "constants.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

struct _SilktexEditor {
    GObject parent_instance;

    GtkSourceView *view;
    GtkSourceBuffer *buffer;
    GtkSourceStyleSchemeManager *style_manager;
    GtkCssProvider *css_provider;

    char *filename;
    char *workfile;
    char *pdffile;
    char *fdname;
    int workfd;
    time_t last_modtime;

    GtkTextTag *error_tag;
    GtkTextTag *search_tag;

    char *search_term;
    gboolean search_backwards;
    gboolean search_whole_word;
    gboolean search_match_case;
};

G_DEFINE_FINAL_TYPE(SilktexEditor, silktex_editor, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_MODIFIED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum {
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static const char style_commands[][3][20] = {
    { "bold", "\\textbf{", "}" },
    { "italic", "\\emph{", "}" },
    { "underline", "\\underline{", "}" },
    { "left", "\\begin{flushleft}", "\\end{flushleft}"},
    { "center", "\\begin{center}", "\\end{center}"},
    { "right", "\\begin{flushright}", "\\end{flushright}"}
};

static void
on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    SilktexEditor *self = SILKTEX_EDITOR(user_data);
    g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODIFIED]);
}

static void
silktex_editor_init_workfile(SilktexEditor *self)
{
    if (!g_file_test(C_TMPDIR, G_FILE_TEST_IS_DIR)) {
        g_mkdir_with_parents(C_TMPDIR, 0755);
    }

    self->fdname = g_build_filename(C_TMPDIR, "silktex_XXXXXX", NULL);
    self->workfd = g_mkstemp(self->fdname);

    if (self->filename != NULL) {
        char *base = g_path_get_basename(self->filename);
        char *dir = g_path_get_dirname(self->filename);
        self->workfile = g_strdup_printf("%s%c.%s.swp", dir, G_DIR_SEPARATOR, base);
        self->pdffile = g_strdup_printf("%s%c.%s.pdf", C_TMPDIR, G_DIR_SEPARATOR, base);
        g_free(base);
        g_free(dir);
    } else {
        self->workfile = g_strdup(self->fdname);
        self->pdffile = g_strdup_printf("%s.pdf", self->fdname);
    }
}

static void
silktex_editor_cleanup_workfile(SilktexEditor *self)
{
    if (self->workfd >= 0) {
        close(self->workfd);
        self->workfd = -1;
    }

    if (self->fdname) {
        g_remove(self->fdname);
        g_clear_pointer(&self->fdname, g_free);
    }
    if (self->workfile) {
        g_remove(self->workfile);
        g_clear_pointer(&self->workfile, g_free);
    }
    if (self->pdffile) {
        g_remove(self->pdffile);
        g_clear_pointer(&self->pdffile, g_free);
    }
}

static void
silktex_editor_dispose(GObject *object)
{
    SilktexEditor *self = SILKTEX_EDITOR(object);

    g_clear_object(&self->css_provider);
    g_clear_pointer(&self->filename, g_free);
    g_clear_pointer(&self->search_term, g_free);

    silktex_editor_cleanup_workfile(self);

    G_OBJECT_CLASS(silktex_editor_parent_class)->dispose(object);
}

static void
silktex_editor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    SilktexEditor *self = SILKTEX_EDITOR(object);

    switch (prop_id) {
        case PROP_MODIFIED:
            g_value_set_boolean(value, silktex_editor_get_modified(self));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
silktex_editor_class_init(SilktexEditorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = silktex_editor_dispose;
    object_class->get_property = silktex_editor_get_property;

    properties[PROP_MODIFIED] =
        g_param_spec_boolean("modified", NULL, NULL, FALSE, G_PARAM_READABLE);

    g_object_class_install_properties(object_class, N_PROPS, properties);

    signals[SIGNAL_CHANGED] =
        g_signal_new("changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);
}

static void
silktex_editor_init(SilktexEditor *self)
{
    self->workfd = -1;

    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage *lang = gtk_source_language_manager_get_language(lm, "latex");

    self->buffer = gtk_source_buffer_new_with_language(lang);
    self->view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(self->buffer));
    self->style_manager = gtk_source_style_scheme_manager_get_default();

    gtk_source_view_set_show_line_numbers(self->view, TRUE);
    gtk_source_view_set_highlight_current_line(self->view, TRUE);
    gtk_source_view_set_auto_indent(self->view, TRUE);
    gtk_source_view_set_tab_width(self->view, 4);
    gtk_source_view_set_insert_spaces_instead_of_tabs(self->view, FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(self->view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_vexpand(GTK_WIDGET(self->view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->view), TRUE);

    self->css_provider = gtk_css_provider_new();
    gtk_widget_add_css_class(GTK_WIDGET(self->view), "silktex-editor");

    GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(self->style_manager, "Adwaita");
    if (scheme != NULL) {
        gtk_source_buffer_set_style_scheme(self->buffer, scheme);
    }

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(self->buffer));
    self->error_tag = gtk_text_tag_new("error");
    self->search_tag = gtk_text_tag_new("search");
    g_object_set(self->error_tag, "background", "red", "foreground", "white", NULL);
    g_object_set(self->search_tag, "background", "yellow", "foreground", "black", NULL);
    gtk_text_tag_table_add(tag_table, self->error_tag);
    gtk_text_tag_table_add(tag_table, self->search_tag);

    g_signal_connect(self->buffer, "changed", G_CALLBACK(on_buffer_changed), self);

    silktex_editor_init_workfile(self);
}

SilktexEditor *
silktex_editor_new(void)
{
    return g_object_new(SILKTEX_TYPE_EDITOR, NULL);
}

GtkWidget *
silktex_editor_get_view(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);
    return GTK_WIDGET(self->view);
}

GtkSourceBuffer *
silktex_editor_get_buffer(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);
    return self->buffer;
}

void
silktex_editor_set_filename(SilktexEditor *self, const char *filename)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    g_free(self->filename);
    self->filename = g_strdup(filename);

    silktex_editor_cleanup_workfile(self);
    silktex_editor_init_workfile(self);

    if (filename != NULL) {
        struct stat attr;
        if (stat(filename, &attr) == 0) {
            self->last_modtime = attr.st_mtime;
        }
    }
}

const char *
silktex_editor_get_filename(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);
    return self->filename;
}

char *
silktex_editor_get_basename(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);

    if (self->filename != NULL) {
        return g_path_get_basename(self->filename);
    }
    return g_strdup(_("Untitled"));
}

void
silktex_editor_load_file(SilktexEditor *self, GFile *file)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));
    g_return_if_fail(G_IS_FILE(file));

    GError *error = NULL;
    char *contents = NULL;
    gsize length = 0;

    if (!g_file_load_contents(file, NULL, &contents, &length, NULL, &error)) {
        g_warning("Failed to load file: %s", error->message);
        g_error_free(error);
        return;
    }

    char *path = g_file_get_path(file);
    silktex_editor_set_filename(self, path);
    g_free(path);

    silktex_editor_set_text(self, contents, (gssize)length);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(self->buffer), FALSE);

    g_free(contents);
}

gboolean
silktex_editor_save_file(SilktexEditor *self, GFile *file, GError **error)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), FALSE);
    g_return_val_if_fail(G_IS_FILE(file), FALSE);

    g_autofree char *text = silktex_editor_get_text(self);

    gboolean result = g_file_replace_contents(file, text, strlen(text),
                                               NULL, FALSE,
                                               G_FILE_CREATE_NONE,
                                               NULL, NULL, error);
    if (result) {
        char *path = g_file_get_path(file);
        silktex_editor_set_filename(self, path);
        g_free(path);
        gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(self->buffer), FALSE);
    }

    return result;
}

void
silktex_editor_set_text(SilktexEditor *self, const char *text, gssize len)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(self->buffer));
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(self->buffer), text, (int)len);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(self->buffer));

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(self->buffer), &start);
    gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(self->buffer), &start);
}

char *
silktex_editor_get_text(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(self->buffer), &start, &end);
    return gtk_text_iter_get_text(&start, &end);
}

gboolean
silktex_editor_get_modified(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), FALSE);
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(self->buffer));
}

void
silktex_editor_set_modified(SilktexEditor *self, gboolean modified)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(self->buffer), modified);
}

void
silktex_editor_undo(SilktexEditor *self)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));
    gtk_text_buffer_undo(GTK_TEXT_BUFFER(self->buffer));
    silktex_editor_scroll_to_cursor(self);
}

void
silktex_editor_redo(SilktexEditor *self)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));
    gtk_text_buffer_redo(GTK_TEXT_BUFFER(self->buffer));
    silktex_editor_scroll_to_cursor(self);
}

gboolean
silktex_editor_can_undo(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), FALSE);
    return gtk_text_buffer_get_can_undo(GTK_TEXT_BUFFER(self->buffer));
}

gboolean
silktex_editor_can_redo(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), FALSE);
    return gtk_text_buffer_get_can_redo(GTK_TEXT_BUFFER(self->buffer));
}

void
silktex_editor_set_style_scheme(SilktexEditor *self, const char *scheme_id)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(self->style_manager, scheme_id);
    if (scheme != NULL) {
        gtk_source_buffer_set_style_scheme(self->buffer, scheme);
    }
}

void
silktex_editor_set_font(SilktexEditor *self, const char *font_desc)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    g_autofree char *css = g_strdup_printf(".silktex-editor { font: %s; }", font_desc);
    gtk_css_provider_load_from_string(self->css_provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(self->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void
silktex_editor_scroll_to_line(SilktexEditor *self, int line)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(self->buffer), &iter, line);
    gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(self->buffer), &iter);
    silktex_editor_scroll_to_cursor(self);
}

void
silktex_editor_scroll_to_cursor(SilktexEditor *self)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(self->buffer));
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(self->view), mark, 0.25, FALSE, 0.0, 0.0);
}

void
silktex_editor_apply_textstyle(SilktexEditor *self, const char *style_type)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(self->buffer), &start, &end)) {
        return;
    }

    int selected = -1;
    int num_styles = G_N_ELEMENTS(style_commands);
    for (int i = 0; i < num_styles; i++) {
        if (g_strcmp0(style_commands[i][0], style_type) == 0) {
            selected = i;
            break;
        }
    }

    if (selected < 0) return;

    g_autofree char *selected_text = gtk_text_iter_get_text(&start, &end);
    g_autofree char *new_text = g_strdup_printf("%s%s%s",
                                                 style_commands[selected][1],
                                                 selected_text,
                                                 style_commands[selected][2]);

    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(self->buffer));
    gtk_text_buffer_delete(GTK_TEXT_BUFFER(self->buffer), &start, &end);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(self->buffer), &start, new_text, -1);
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(self->buffer));
}

void
silktex_editor_insert_package(SilktexEditor *self, const char *package, const char *options)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    g_autofree char *pkg_str = NULL;
    if (options == NULL) {
        pkg_str = g_strdup_printf("\\usepackage{%s}\n", package);
    } else {
        pkg_str = g_strdup_printf("\\usepackage[%s]{%s}\n", options, package);
    }

    GtkTextIter start, mstart, mend;
    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(self->buffer), &start);

    if (gtk_text_iter_forward_search(&start, "\\begin{document}", 0, &mstart, &mend, NULL)) {
        gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(self->buffer));
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(self->buffer), &mstart, pkg_str, -1);
        gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(self->buffer));
    }
}

void
silktex_editor_search(SilktexEditor *self, const char *term, gboolean backwards,
                      gboolean whole_word, gboolean match_case)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    g_free(self->search_term);
    self->search_term = g_strdup(term);
    self->search_backwards = backwards;
    self->search_whole_word = whole_word;
    self->search_match_case = match_case;

    silktex_editor_search_next(self, FALSE);
}

void
silktex_editor_search_next(SilktexEditor *self, gboolean backwards)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    if (self->search_term == NULL) return;

    GtkTextIter current, mstart, mend;
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(self->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(self->buffer), &current, mark);

    GtkTextSearchFlags flags = 0;
    if (!self->search_match_case) {
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    }

    gboolean found = FALSE;
    if (self->search_backwards ^ backwards) {
        found = gtk_text_iter_backward_search(&current, self->search_term, flags, &mstart, &mend, NULL);
    } else {
        gtk_text_iter_forward_chars(&current, (int)strlen(self->search_term));
        found = gtk_text_iter_forward_search(&current, self->search_term, flags, &mstart, &mend, NULL);
    }

    if (found) {
        gtk_text_buffer_select_range(GTK_TEXT_BUFFER(self->buffer), &mstart, &mend);
        silktex_editor_scroll_to_cursor(self);
    }
}

void
silktex_editor_replace(SilktexEditor *self, const char *term, const char *replacement,
                       gboolean backwards, gboolean whole_word, gboolean match_case)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(self->buffer), &start, &end)) {
        g_autofree char *selected = gtk_text_iter_get_text(&start, &end);
        gboolean matches = match_case ? g_strcmp0(selected, term) == 0
                                      : g_ascii_strcasecmp(selected, term) == 0;
        if (matches) {
            gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(self->buffer));
            gtk_text_buffer_delete(GTK_TEXT_BUFFER(self->buffer), &start, &end);
            gtk_text_buffer_insert(GTK_TEXT_BUFFER(self->buffer), &start, replacement, -1);
            gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(self->buffer));
        }
    }

    silktex_editor_search(self, term, backwards, whole_word, match_case);
}

void
silktex_editor_replace_all(SilktexEditor *self, const char *term, const char *replacement,
                            gboolean whole_word, gboolean match_case)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    GtkTextIter start, mstart, mend;
    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(self->buffer), &start);

    GtkTextSearchFlags flags = 0;
    if (!match_case) {
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    }

    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(self->buffer));
    while (gtk_text_iter_forward_search(&start, term, flags, &mstart, &mend, NULL)) {
        gtk_text_buffer_delete(GTK_TEXT_BUFFER(self->buffer), &mstart, &mend);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(self->buffer), &mstart, replacement, -1);
        start = mstart;
    }
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(self->buffer));
}

void
silktex_editor_goto_line(SilktexEditor *self, int line)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(self->buffer), &it, line);
    gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(self->buffer), &it);
    silktex_editor_scroll_to_cursor(self);
}

int
silktex_editor_get_cursor_line(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), 0);
    GtkTextIter it;
    GtkTextMark *mark = gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(self->buffer));
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(self->buffer), &it, mark);
    return gtk_text_iter_get_line(&it);
}

void
silktex_editor_apply_settings(SilktexEditor *self)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    gtk_source_view_set_show_line_numbers(self->view,
        config_get_boolean("Editor", "line_numbers"));
    gtk_source_view_set_highlight_current_line(self->view,
        config_get_boolean("Editor", "highlighting"));
    gtk_source_view_set_auto_indent(self->view,
        config_get_boolean("Editor", "autoindentation"));
    gtk_source_view_set_insert_spaces_instead_of_tabs(self->view,
        config_get_boolean("Editor", "spaces_instof_tabs"));
    gtk_source_view_set_tab_width(self->view,
        (guint)config_get_integer("Editor", "tabwidth"));

    GtkWrapMode wrap = GTK_WRAP_NONE;
    if (config_get_boolean("Editor", "textwrapping")) {
        wrap = config_get_boolean("Editor", "wordwrapping")
               ? GTK_WRAP_WORD : GTK_WRAP_CHAR;
    }
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->view), wrap);

    const char *scheme = config_get_string("Editor", "style_scheme");
    if (scheme && *scheme)
        silktex_editor_set_style_scheme(self, scheme);
}

const char *
silktex_editor_get_workfile(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);
    return self->workfile;
}

const char *
silktex_editor_get_pdffile(SilktexEditor *self)
{
    g_return_val_if_fail(SILKTEX_IS_EDITOR(self), NULL);
    return self->pdffile;
}

void
silktex_editor_update_workfile(SilktexEditor *self)
{
    g_return_if_fail(SILKTEX_IS_EDITOR(self));

    if (self->workfile == NULL) return;

    g_autofree char *text = silktex_editor_get_text(self);
    GError *error = NULL;

    if (!g_file_set_contents(self->workfile, text, -1, &error)) {
        g_warning("Failed to update workfile: %s", error->message);
        g_error_free(error);
    }
}
