/*
 * SilkTex - Preferences dialog
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * All settings are written immediately to configfile and the apply-callback
 * is fired so the window can push them to every open editor.
 */
#include "prefs.h"
#include "configfile.h"
#include "constants.h"
#include "utils.h"
#include <glib/gi18n.h>
#include <gtksourceview/gtksource.h>

struct _SilktexPrefs {
    AdwPreferencesDialog parent_instance;

    SilktexPrefsApplyFunc apply_func;
    gpointer apply_data;

    /* Editor page widgets we need to read back */
    AdwSwitchRow *row_line_numbers;
    AdwSwitchRow *row_highlighting;
    AdwSwitchRow *row_textwrap;
    AdwSwitchRow *row_wordwrap;
    AdwSwitchRow *row_autoindent;
    AdwSwitchRow *row_spaces_tabs;
    AdwSpinRow *row_tabwidth;
    AdwComboRow *row_scheme;

    /* Compile page */
    AdwComboRow *row_typesetter;
    AdwSwitchRow *row_shellescape;
    AdwSwitchRow *row_synctex;
    AdwSwitchRow *row_auto_compile;
    AdwSpinRow *row_compile_timer;

    /* Files page */
    AdwSwitchRow *row_autosave;
    AdwSpinRow *row_autosave_timer;

    /* Snippets page */
    SilktexSnippets *snippets;
    GtkTextBuffer *snippet_buf; /* buffer for the inline snippet editor */
    AdwComboRow *row_snippet_pick;
    AdwEntryRow *row_snippet_name;
    AdwEntryRow *row_snippet_key;
    AdwEntryRow *row_snippet_accel;
    AdwComboRow *row_snippet_mod1;
    AdwComboRow *row_snippet_mod2;
    GPtrArray *snippet_entries; /* SnippetEntry* */
    guint current_snippet_index;
    gboolean snippets_updating_ui; /* guard: block feedback loops while we re-populate the snippet
                                      widgets */

    GtkStringList *scheme_ids; /* parallel to the combo model */
};

typedef struct {
    char *name;
    char *key;
    char *accel;
    char *body;
} SnippetEntry;

/* forward declarations – definitions live further down next to their peers */
static char *extract_accel_letter(const char *accel);
static void snippet_update_accel_subtitle(SilktexPrefs *self);
static void snippets_apply_modifiers(SilktexPrefs *self);

G_DEFINE_FINAL_TYPE (SilktexPrefs, silktex_prefs, ADW_TYPE_PREFERENCES_DIALOG)

    /* ------------------------------------------------------------------ helpers */

    static void fire_apply(SilktexPrefs *self)
    {
        if (self->apply_func) self->apply_func(self->apply_data);
    }

/* Build a GtkStringList whose display names match the style scheme list.
 * Also fill self->scheme_ids with the corresponding IDs. */
static GtkStringList *build_scheme_model(SilktexPrefs *self)
{
    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();
    const char *const *ids = gtk_source_style_scheme_manager_get_scheme_ids(mgr);

    /* add user scheme dir */
    g_autofree char *confdir = C_GUMMI_CONFDIR;
    g_autofree char *custom = g_build_filename(confdir, "styles", NULL);
    if (g_file_test(custom, G_FILE_TEST_IS_DIR))
        gtk_source_style_scheme_manager_append_search_path(mgr, custom);

    GtkStringList *names = gtk_string_list_new(NULL);
    self->scheme_ids = gtk_string_list_new(NULL);

    for (int i = 0; ids && ids[i]; i++) {
        GtkSourceStyleScheme *s = gtk_source_style_scheme_manager_get_scheme(mgr, ids[i]);
        gtk_string_list_append(names, gtk_source_style_scheme_get_name(s));
        gtk_string_list_append(self->scheme_ids, ids[i]);
    }
    return names;
}

static int scheme_index_for_id(SilktexPrefs *self, const char *id)
{
    guint n = g_list_model_get_n_items(G_LIST_MODEL(self->scheme_ids));
    for (guint i = 0; i < n; i++) {
        GtkStringObject *obj = g_list_model_get_item(G_LIST_MODEL(self->scheme_ids), i);
        if (g_strcmp0(gtk_string_object_get_string(obj), id) == 0) {
            g_object_unref(obj);
            return (int)i;
        }
        g_object_unref(obj);
    }
    return 0;
}

static void snippet_entry_free(gpointer data)
{
    SnippetEntry *e = data;
    if (!e) return;
    g_free(e->name);
    g_free(e->key);
    g_free(e->accel);
    g_free(e->body);
    g_free(e);
}

static gboolean snippet_sync_current(SilktexPrefs *self)
{
    if (!self->snippet_entries || self->snippet_entries->len == 0) return FALSE;
    if (self->current_snippet_index >= self->snippet_entries->len) return FALSE;

    SnippetEntry *e = g_ptr_array_index(self->snippet_entries, self->current_snippet_index);
    if (!e) return FALSE;

    g_free(e->key);
    g_free(e->accel);
    g_free(e->body);
    e->key = g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->row_snippet_key)));
    e->accel = g_strdup(gtk_editable_get_text(GTK_EDITABLE(self->row_snippet_accel)));

    GtkTextIter s, t;
    gtk_text_buffer_get_bounds(self->snippet_buf, &s, &t);
    e->body = gtk_text_buffer_get_text(self->snippet_buf, &s, &t, FALSE);
    return TRUE;
}

static void snippet_load_current_into_ui(SilktexPrefs *self)
{
    self->snippets_updating_ui = TRUE;
    if (!self->snippet_entries || self->snippet_entries->len == 0) {
        gtk_editable_set_text(GTK_EDITABLE(self->row_snippet_name), "");
        gtk_editable_set_text(GTK_EDITABLE(self->row_snippet_key), "");
        gtk_editable_set_text(GTK_EDITABLE(self->row_snippet_accel), "");
        gtk_text_buffer_set_text(self->snippet_buf, "", -1);
        self->snippets_updating_ui = FALSE;
        return;
    }
    if (self->current_snippet_index >= self->snippet_entries->len) self->current_snippet_index = 0;

    SnippetEntry *e = g_ptr_array_index(self->snippet_entries, self->current_snippet_index);
    gtk_editable_set_text(GTK_EDITABLE(self->row_snippet_name), e->name ? e->name : "");
    gtk_editable_set_text(GTK_EDITABLE(self->row_snippet_key), e->key ? e->key : "");
    /* Only the letter portion is user-editable; modifier prefix is global. */
    g_autofree char *letter = extract_accel_letter(e->accel);
    gtk_editable_set_text(GTK_EDITABLE(self->row_snippet_accel), letter);
    gtk_text_buffer_set_text(self->snippet_buf, e->body ? e->body : "", -1);
    self->snippets_updating_ui = FALSE;
    snippet_update_accel_subtitle(self);
}

static void snippets_rebuild_combo(SilktexPrefs *self)
{
    self->snippets_updating_ui = TRUE;
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint i = 0; self->snippet_entries && i < self->snippet_entries->len; i++) {
        SnippetEntry *e = g_ptr_array_index(self->snippet_entries, i);
        gtk_string_list_append(model, (e->name && *e->name) ? e->name : _("Unnamed"));
    }
    adw_combo_row_set_model(self->row_snippet_pick, G_LIST_MODEL(model));
    g_object_unref(model);

    if (self->snippet_entries && self->snippet_entries->len > 0)
        adw_combo_row_set_selected(self->row_snippet_pick, self->current_snippet_index);
    else
        adw_combo_row_set_selected(self->row_snippet_pick, GTK_INVALID_LIST_POSITION);
    self->snippets_updating_ui = FALSE;
}

static void snippets_parse_file(SilktexPrefs *self)
{
    g_clear_pointer(&self->snippet_entries, g_ptr_array_unref);
    self->snippet_entries = g_ptr_array_new_with_free_func(snippet_entry_free);

    if (!self->snippets) return;

    const char *fname = silktex_snippets_get_filename(self->snippets);
    g_autofree char *text = NULL;
    if (!g_file_get_contents(fname, &text, NULL, NULL) || !text) return;

    gchar **lines = g_strsplit(text, "\n", -1);
    SnippetEntry *cur = NULL;
    for (int i = 0; lines[i]; i++) {
        const char *ln = lines[i];
        if (g_str_has_prefix(ln, "snippet ")) {
            gchar **hdr = g_strsplit(ln + 8, ",", 3);
            cur = g_new0(SnippetEntry, 1);
            cur->key = g_strdup(hdr[0] ? hdr[0] : "");
            cur->accel = g_strdup(hdr[1] ? hdr[1] : "");
            cur->name = g_strdup(hdr[2] ? hdr[2] : (hdr[0] ? hdr[0] : ""));
            cur->body = g_strdup("");
            g_ptr_array_add(self->snippet_entries, cur);
            g_strfreev(hdr);
            continue;
        }
        if (cur && ln[0] == '\t') {
            const char *part = ln + 1;
            char *old = cur->body;
            cur->body = (old && *old) ? g_strconcat(old, "\n", part, NULL) : g_strdup(part);
            g_free(old);
        }
    }
    g_strfreev(lines);
}

static gboolean snippets_write_file(SilktexPrefs *self, GError **error)
{
    GString *out = g_string_new(NULL);
    for (guint i = 0; self->snippet_entries && i < self->snippet_entries->len; i++) {
        SnippetEntry *e = g_ptr_array_index(self->snippet_entries, i);
        if (!e->key || !e->name) continue;
        g_string_append_printf(out, "snippet %s,%s,%s\n", e->key ? e->key : "",
                               e->accel ? e->accel : "", e->name ? e->name : "");
        if (e->body && *e->body) {
            gchar **blines = g_strsplit(e->body, "\n", -1);
            for (int li = 0; blines[li]; li++)
                g_string_append_printf(out, "\t%s\n", blines[li]);
            g_strfreev(blines);
        } else {
            g_string_append(out, "\t\n");
        }
    }
    gboolean ok =
        g_file_set_contents(silktex_snippets_get_filename(self->snippets), out->str, -1, error);
    g_string_free(out, TRUE);
    return ok;
}

/* ------------------------------------------------------------------ signals */

static void on_line_numbers(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Editor", "line_numbers", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_highlighting(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Editor", "highlighting", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_textwrap(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    gboolean val = adw_switch_row_get_active(r);
    config_set_boolean("Editor", "textwrapping", val);
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_wordwrap), val);
    fire_apply(self);
}
static void on_wordwrap(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Editor", "wordwrapping", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_autoindent(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Editor", "autoindentation", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_spaces_tabs(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Editor", "spaces_instof_tabs", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_tabwidth(AdwSpinRow *r, GParamSpec *p, gpointer ud)
{
    config_set_integer("Editor", "tabwidth", (int)adw_spin_row_get_value(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_scheme(AdwComboRow *r, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    guint idx = adw_combo_row_get_selected(r);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    GtkStringObject *obj = g_list_model_get_item(G_LIST_MODEL(self->scheme_ids), idx);
    if (!obj) return;
    config_set_string("Editor", "style_scheme", gtk_string_object_get_string(obj));
    g_object_unref(obj);
    fire_apply(self);
}

static void on_shellescape(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Compile", "shellescape", adw_switch_row_get_active(r));
}
static void on_synctex(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Compile", "synctex", adw_switch_row_get_active(r));
}
static void on_auto_compile(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    config_set_boolean("Compile", "auto_compile", adw_switch_row_get_active(r));
}
static void on_compile_timer(AdwSpinRow *r, GParamSpec *p, gpointer ud)
{
    config_set_integer("Compile", "timer", (int)adw_spin_row_get_value(r));
}
static void on_typesetter(AdwComboRow *r, GParamSpec *p, gpointer ud)
{
    static const char *ts[] = {"pdflatex", "xelatex", "lualatex", "latexmk"};
    guint idx = adw_combo_row_get_selected(r);
    if (idx < G_N_ELEMENTS(ts)) config_set_string("Compile", "typesetter", ts[idx]);
}

static void on_autosave(AdwSwitchRow *r, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    gboolean val = adw_switch_row_get_active(r);
    config_set_boolean("File", "autosaving", val);
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_autosave_timer), val);
}
static void on_autosave_timer(AdwSpinRow *r, GParamSpec *p, gpointer ud)
{
    config_set_integer("File", "autosave_timer", (int)adw_spin_row_get_value(r));
}

/* ------------------------------------------------------------------ init */

static void silktex_prefs_init(SilktexPrefs *self)
{
    /* ---- Editor page ---- */
    AdwPreferencesPage *editor_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(editor_page, _("Editor"));
    adw_preferences_page_set_icon_name(editor_page, "document-edit-symbolic");

    AdwPreferencesGroup *grp_display = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_display, _("Display"));

    self->row_line_numbers = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_line_numbers),
                                  _("Show Line Numbers"));
    adw_switch_row_set_active(self->row_line_numbers, config_get_boolean("Editor", "line_numbers"));
    g_signal_connect(self->row_line_numbers, "notify::active", G_CALLBACK(on_line_numbers), self);

    self->row_highlighting = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_highlighting),
                                  _("Highlight Current Line"));
    adw_switch_row_set_active(self->row_highlighting, config_get_boolean("Editor", "highlighting"));
    g_signal_connect(self->row_highlighting, "notify::active", G_CALLBACK(on_highlighting), self);

    self->row_textwrap = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_textwrap), _("Text Wrapping"));
    adw_switch_row_set_active(self->row_textwrap, config_get_boolean("Editor", "textwrapping"));
    g_signal_connect(self->row_textwrap, "notify::active", G_CALLBACK(on_textwrap), self);

    self->row_wordwrap = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_wordwrap), _("Word Wrapping"));
    adw_switch_row_set_active(self->row_wordwrap, config_get_boolean("Editor", "wordwrapping"));
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_wordwrap),
                             config_get_boolean("Editor", "textwrapping"));
    g_signal_connect(self->row_wordwrap, "notify::active", G_CALLBACK(on_wordwrap), self);

    adw_preferences_group_add(grp_display, GTK_WIDGET(self->row_line_numbers));
    adw_preferences_group_add(grp_display, GTK_WIDGET(self->row_highlighting));
    adw_preferences_group_add(grp_display, GTK_WIDGET(self->row_textwrap));
    adw_preferences_group_add(grp_display, GTK_WIDGET(self->row_wordwrap));

    AdwPreferencesGroup *grp_input = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_input, _("Input"));

    self->row_autoindent = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_autoindent), _("Auto Indentation"));
    adw_switch_row_set_active(self->row_autoindent,
                              config_get_boolean("Editor", "autoindentation"));
    g_signal_connect(self->row_autoindent, "notify::active", G_CALLBACK(on_autoindent), self);

    self->row_spaces_tabs = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_spaces_tabs),
                                  _("Spaces Instead of Tabs"));
    adw_switch_row_set_active(self->row_spaces_tabs,
                              config_get_boolean("Editor", "spaces_instof_tabs"));
    g_signal_connect(self->row_spaces_tabs, "notify::active", G_CALLBACK(on_spaces_tabs), self);

    self->row_tabwidth = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 16, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_tabwidth), _("Tab Width"));
    adw_spin_row_set_value(self->row_tabwidth, config_get_integer("Editor", "tabwidth"));
    g_signal_connect(self->row_tabwidth, "notify::value", G_CALLBACK(on_tabwidth), self);

    adw_preferences_group_add(grp_input, GTK_WIDGET(self->row_autoindent));
    adw_preferences_group_add(grp_input, GTK_WIDGET(self->row_spaces_tabs));
    adw_preferences_group_add(grp_input, GTK_WIDGET(self->row_tabwidth));

    AdwPreferencesGroup *grp_scheme = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_scheme, _("Color Scheme"));

    GtkStringList *scheme_names = build_scheme_model(self);
    self->row_scheme = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_scheme), _("Style Scheme"));
    adw_combo_row_set_model(self->row_scheme, G_LIST_MODEL(scheme_names));
    int si = scheme_index_for_id(self, config_get_string("Editor", "style_scheme"));
    adw_combo_row_set_selected(self->row_scheme, (guint)si);
    g_signal_connect(self->row_scheme, "notify::selected", G_CALLBACK(on_scheme), self);

    adw_preferences_group_add(grp_scheme, GTK_WIDGET(self->row_scheme));

    adw_preferences_page_add(editor_page, grp_display);
    adw_preferences_page_add(editor_page, grp_input);
    adw_preferences_page_add(editor_page, grp_scheme);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), editor_page);

    /* ---- Compile page ---- */
    AdwPreferencesPage *compile_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(compile_page, _("Compilation"));
    adw_preferences_page_set_icon_name(compile_page, "system-run-symbolic");

    AdwPreferencesGroup *grp_ts = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_ts, _("Typesetter"));

    GtkStringList *ts_list =
        gtk_string_list_new((const char *[]){"pdflatex", "xelatex", "lualatex", "latexmk", NULL});

    self->row_typesetter = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_typesetter), _("Typesetter"));
    adw_combo_row_set_model(self->row_typesetter, G_LIST_MODEL(ts_list));

    const char *cur_ts = config_get_string("Compile", "typesetter");
    static const char *ts_vals[] = {"pdflatex", "xelatex", "lualatex", "latexmk"};
    for (int i = 0; i < (int)G_N_ELEMENTS(ts_vals); i++) {
        if (g_strcmp0(cur_ts, ts_vals[i]) == 0) {
            adw_combo_row_set_selected(self->row_typesetter, (guint)i);
            break;
        }
    }
    g_signal_connect(self->row_typesetter, "notify::selected", G_CALLBACK(on_typesetter), self);

    self->row_shellescape = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_shellescape),
                                  _("Shell Escape (-shell-escape)"));
    adw_switch_row_set_active(self->row_shellescape, config_get_boolean("Compile", "shellescape"));
    g_signal_connect(self->row_shellescape, "notify::active", G_CALLBACK(on_shellescape), self);

    self->row_synctex = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_synctex), _("SyncTeX Support"));
    adw_switch_row_set_active(self->row_synctex, config_get_boolean("Compile", "synctex"));
    g_signal_connect(self->row_synctex, "notify::active", G_CALLBACK(on_synctex), self);

    adw_preferences_group_add(grp_ts, GTK_WIDGET(self->row_typesetter));
    adw_preferences_group_add(grp_ts, GTK_WIDGET(self->row_shellescape));
    adw_preferences_group_add(grp_ts, GTK_WIDGET(self->row_synctex));

    AdwPreferencesGroup *grp_timing = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_timing, _("Auto-Compile"));

    self->row_auto_compile = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_auto_compile),
                                  _("Auto-Compile on Edit"));
    adw_switch_row_set_active(self->row_auto_compile,
                              config_get_boolean("Compile", "auto_compile"));
    g_signal_connect(self->row_auto_compile, "notify::active", G_CALLBACK(on_auto_compile), self);

    self->row_compile_timer = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 30, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_compile_timer),
                                  _("Compile Delay (seconds)"));
    adw_spin_row_set_value(self->row_compile_timer, config_get_integer("Compile", "timer"));
    g_signal_connect(self->row_compile_timer, "notify::value", G_CALLBACK(on_compile_timer), self);

    adw_preferences_group_add(grp_timing, GTK_WIDGET(self->row_auto_compile));
    adw_preferences_group_add(grp_timing, GTK_WIDGET(self->row_compile_timer));

    adw_preferences_page_add(compile_page, grp_ts);
    adw_preferences_page_add(compile_page, grp_timing);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), compile_page);

    /* ---- Files page ---- */
    AdwPreferencesPage *files_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(files_page, _("Files"));
    adw_preferences_page_set_icon_name(files_page, "document-save-symbolic");

    AdwPreferencesGroup *grp_autosave = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_autosave, _("Auto-Save"));

    self->row_autosave = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_autosave), _("Enable Auto-Save"));
    adw_switch_row_set_active(self->row_autosave, config_get_boolean("File", "autosaving"));
    g_signal_connect(self->row_autosave, "notify::active", G_CALLBACK(on_autosave), self);

    self->row_autosave_timer = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 60, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_autosave_timer),
                                  _("Auto-Save Interval (minutes)"));
    adw_spin_row_set_value(self->row_autosave_timer, config_get_integer("File", "autosave_timer"));
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_autosave_timer),
                             config_get_boolean("File", "autosaving"));
    g_signal_connect(self->row_autosave_timer, "notify::value", G_CALLBACK(on_autosave_timer),
                     self);

    adw_preferences_group_add(grp_autosave, GTK_WIDGET(self->row_autosave));
    adw_preferences_group_add(grp_autosave, GTK_WIDGET(self->row_autosave_timer));
    adw_preferences_page_add(files_page, grp_autosave);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), files_page);

    /* ---- Snippets page ---- (content populated lazily in set_snippets) */
    AdwPreferencesPage *snip_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(snip_page, _("Snippets"));
    adw_preferences_page_set_icon_name(snip_page, "starred-symbolic");
    g_object_set_data(G_OBJECT(self), "snip-page", snip_page);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), snip_page);
}

static void silktex_prefs_dispose(GObject *obj)
{
    SilktexPrefs *self = SILKTEX_PREFS(obj);
    g_clear_object(&self->snippets);
    g_clear_object(&self->scheme_ids);
    g_clear_pointer(&self->snippet_entries, g_ptr_array_unref);
    G_OBJECT_CLASS(silktex_prefs_parent_class)->dispose(obj);
}

static void silktex_prefs_class_init(SilktexPrefsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = silktex_prefs_dispose;
}

SilktexPrefs *silktex_prefs_new(void)
{
    return g_object_new(SILKTEX_TYPE_PREFS, NULL);
}

void silktex_prefs_set_apply_callback(SilktexPrefs *self, SilktexPrefsApplyFunc func,
                                      gpointer user_data)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    self->apply_func = func;
    self->apply_data = user_data;
}

/* ---- snippet editor callbacks ---- */

static void on_snippet_save(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (!self->snippets || !self->snippet_buf) return;
    snippet_sync_current(self);
    GError *err = NULL;
    if (!snippets_write_file(self, &err)) {
        g_warning("Failed to save snippets: %s", err->message);
        g_error_free(err);
    } else {
        silktex_snippets_reload(self->snippets);
        gtk_text_buffer_set_modified(self->snippet_buf, FALSE);
    }
}

static void on_snippet_reset(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (!self->snippets || !self->snippet_buf) return;
    silktex_snippets_reset_to_default(self->snippets);
    snippets_parse_file(self);
    self->current_snippet_index = 0;
    snippets_rebuild_combo(self);
    snippet_load_current_into_ui(self);
}

/* ---- global snippet modifier preferences ------------------------------------
 *
 * Each snippet in snippets.cfg stores only a single letter (or keysym name)
 * in its ACCELERATOR field.  At runtime that letter is combined with the two
 * global modifier keys chosen here to form the actual shortcut.
 */

static const struct {
    const char *display; /* shown in the combo */
    const char *config;  /* stored in silktex.ini */
    guint gdk_mask;      /* for the label preview */
} MODIFIER_CHOICES[] = {
    {"—", "", 0},
    {"Shift", "Shift", GDK_SHIFT_MASK},
    {"Control", "Control", GDK_CONTROL_MASK},
    {"Alt", "Alt", GDK_ALT_MASK},
    {"Super", "Super", GDK_SUPER_MASK},
};

static guint modifier_choice_index_for(const char *name)
{
    for (guint i = 0; i < G_N_ELEMENTS(MODIFIER_CHOICES); i++)
        if (g_strcmp0(name ? name : "", MODIFIER_CHOICES[i].config) == 0) return i;
    return 0;
}

static const char *modifier_choice_config(guint idx)
{
    if (idx >= G_N_ELEMENTS(MODIFIER_CHOICES)) idx = 0;
    return MODIFIER_CHOICES[idx].config;
}

static GtkStringList *build_modifier_model(void)
{
    GtkStringList *m = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(MODIFIER_CHOICES); i++)
        gtk_string_list_append(m, MODIFIER_CHOICES[i].display);
    return m;
}

/*
 * Extract the single-letter/keysym portion from any accelerator string.
 * Accepts "e", "<Shift><Alt>e", "F3" etc. — returns an allocated string
 * with just the key name (never NULL).
 */
static char *extract_accel_letter(const char *accel)
{
    if (!accel) return g_strdup("");
    const char *p = accel;
    while (*p == '<') {
        const char *close = strchr(p, '>');
        if (!close) break;
        p = close + 1;
    }
    return g_strdup(p);
}

/* Apply the current modifier preferences to the running snippet engine. */
static void snippets_apply_modifiers(SilktexPrefs *self)
{
    if (!self->snippets) return;
    const char *m1 = config_get_string("Snippets", "modifier1");
    const char *m2 = config_get_string("Snippets", "modifier2");
    silktex_snippets_set_modifiers(self->snippets, m1, m2);
}

static void snippet_update_accel_subtitle(SilktexPrefs *self)
{
    if (!self->row_snippet_accel) return;
    const char *letter = gtk_editable_get_text(GTK_EDITABLE(self->row_snippet_accel));
    const char *m1 = config_get_string("Snippets", "modifier1");
    const char *m2 = config_get_string("Snippets", "modifier2");

    GString *s = g_string_new(NULL);
    if (letter && *letter) {
        if (m1 && *m1) g_string_append_printf(s, "%s+", m1);
        if (m2 && *m2) g_string_append_printf(s, "%s+", m2);
        g_string_append(s, letter);
    } else {
        g_string_append(s, _("No shortcut"));
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->row_snippet_accel), s->str);
    g_string_free(s, TRUE);
}

static void on_snippet_modifier_changed(AdwComboRow *row, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (self->snippets_updating_ui) return;

    guint i1 = adw_combo_row_get_selected(self->row_snippet_mod1);
    guint i2 = adw_combo_row_get_selected(self->row_snippet_mod2);
    config_set_string("Snippets", "modifier1", modifier_choice_config(i1));
    config_set_string("Snippets", "modifier2", modifier_choice_config(i2));

    snippets_apply_modifiers(self);
    snippet_update_accel_subtitle(self);
    fire_apply(self);
}

static void on_snippet_accel_changed(AdwEntryRow *row, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (self->snippets_updating_ui) return;
    if (!self->snippet_entries || self->current_snippet_index >= self->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(self->snippet_entries, self->current_snippet_index);
    g_free(e->accel);
    e->accel = g_strdup(gtk_editable_get_text(GTK_EDITABLE(row)));
    snippet_update_accel_subtitle(self);
}

static void on_snippet_pick_changed(AdwComboRow *row, GParamSpec *pspec, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (self->snippets_updating_ui) return;
    if (!self->snippet_entries || self->snippet_entries->len == 0) return;
    snippet_sync_current(self);
    guint idx = adw_combo_row_get_selected(row);
    if (idx == GTK_INVALID_LIST_POSITION || idx >= self->snippet_entries->len) return;
    self->current_snippet_index = idx;
    snippet_load_current_into_ui(self);
}

static void on_snippet_name_changed(AdwEntryRow *row, GParamSpec *pspec, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (self->snippets_updating_ui) return;
    if (!self->snippet_entries || self->current_snippet_index >= self->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(self->snippet_entries, self->current_snippet_index);
    g_free(e->name);
    e->name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(row)));
    snippets_rebuild_combo(self);
}

static void on_snippet_new(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    snippet_sync_current(self);
    SnippetEntry *e = g_new0(SnippetEntry, 1);
    e->name = g_strdup(_("New Snippet"));
    e->key = g_strdup("");
    e->accel = g_strdup("");
    e->body = g_strdup("");
    g_ptr_array_add(self->snippet_entries, e);
    self->current_snippet_index = self->snippet_entries->len - 1;
    snippets_rebuild_combo(self);
    snippet_load_current_into_ui(self);
}

static void on_snippet_remove(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (!self->snippet_entries || self->snippet_entries->len == 0) return;
    g_ptr_array_remove_index(self->snippet_entries, self->current_snippet_index);
    if (self->current_snippet_index > 0) self->current_snippet_index--;
    snippets_rebuild_combo(self);
    snippet_load_current_into_ui(self);
}

static void on_snippet_insert_macro(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    const char *macro = g_object_get_data(G_OBJECT(btn), "snippet-macro");
    if (!macro) return;
    GtkTextIter it;
    GtkTextMark *mark = gtk_text_buffer_get_insert(self->snippet_buf);
    gtk_text_buffer_get_iter_at_mark(self->snippet_buf, &it, mark);
    gtk_text_buffer_insert(self->snippet_buf, &it, macro, -1);
}

void silktex_prefs_set_snippets(SilktexPrefs *self, SilktexSnippets *snippets)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    g_set_object(&self->snippets, snippets);

    AdwPreferencesPage *snip_page =
        ADW_PREFERENCES_PAGE(g_object_get_data(G_OBJECT(self), "snip-page"));
    if (!snip_page) return;

    /* ---- shortcut modifier pair (global) ---- */
    AdwPreferencesGroup *grp_mods = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_mods, _("Shortcut Modifiers"));
    adw_preferences_group_set_description(
        grp_mods, _("Pick two modifier keys to combine with each snippet's "
                    "letter.  Press Modifier 1 + Modifier 2 + Letter to expand "
                    "the snippet.  After expansion, Tab cycles through the "
                    "$1, $2, … placeholders; $0 is the final landing position."));

    GtkStringList *mod_model_1 = build_modifier_model();
    GtkStringList *mod_model_2 = build_modifier_model();

    self->row_snippet_mod1 = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_snippet_mod1), _("Modifier 1"));
    adw_combo_row_set_model(self->row_snippet_mod1, G_LIST_MODEL(mod_model_1));
    adw_combo_row_set_selected(self->row_snippet_mod1, modifier_choice_index_for(config_get_string(
                                                           "Snippets", "modifier1")));
    g_signal_connect(self->row_snippet_mod1, "notify::selected",
                     G_CALLBACK(on_snippet_modifier_changed), self);

    self->row_snippet_mod2 = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_snippet_mod2), _("Modifier 2"));
    adw_combo_row_set_model(self->row_snippet_mod2, G_LIST_MODEL(mod_model_2));
    adw_combo_row_set_selected(self->row_snippet_mod2, modifier_choice_index_for(config_get_string(
                                                           "Snippets", "modifier2")));
    g_signal_connect(self->row_snippet_mod2, "notify::selected",
                     G_CALLBACK(on_snippet_modifier_changed), self);

    adw_preferences_group_add(grp_mods, GTK_WIDGET(self->row_snippet_mod1));
    adw_preferences_group_add(grp_mods, GTK_WIDGET(self->row_snippet_mod2));
    adw_preferences_page_add(snip_page, grp_mods);

    /* ---- snippet selection ---- */
    AdwPreferencesGroup *grp_list = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_list, _("Manage Snippets"));

    self->row_snippet_pick = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_snippet_pick), _("Snippet"));
    g_signal_connect(self->row_snippet_pick, "notify::selected",
                     G_CALLBACK(on_snippet_pick_changed), self);

    self->row_snippet_name = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_snippet_name), _("Name"));
    g_signal_connect(self->row_snippet_name, "notify::text", G_CALLBACK(on_snippet_name_changed),
                     self);

    self->row_snippet_key = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_snippet_key), _("Tab Trigger"));

    self->row_snippet_accel = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_snippet_accel),
                                  _("Shortcut Letter"));
    g_signal_connect(self->row_snippet_accel, "notify::text", G_CALLBACK(on_snippet_accel_changed),
                     self);

    adw_preferences_group_add(grp_list, GTK_WIDGET(self->row_snippet_pick));
    adw_preferences_group_add(grp_list, GTK_WIDGET(self->row_snippet_name));
    adw_preferences_group_add(grp_list, GTK_WIDGET(self->row_snippet_key));
    adw_preferences_group_add(grp_list, GTK_WIDGET(self->row_snippet_accel));
    adw_preferences_page_add(snip_page, grp_list);

    /* ---- instruction and editor ---- */
    AdwPreferencesGroup *grp_info = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_info, _("Snippet Body"));
    adw_preferences_group_set_description(
        grp_info, _("Placeholders: $1  $2  …  $0 (final position)   ${N:default}\n"
                    "Macros:  $FILENAME   $BASENAME   $SELECTED_TEXT"));

    /* ---- toolbar row ---- */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_widget_set_margin_bottom(toolbar, 4);

    GtkWidget *btn_save = gtk_button_new_with_label(_("Save"));
    GtkWidget *btn_reset = gtk_button_new_with_label(_("Reset to Default"));
    GtkWidget *btn_new = gtk_button_new_with_label(_("New"));
    GtkWidget *btn_remove = gtk_button_new_with_label(_("Remove"));
    gtk_widget_add_css_class(btn_save, "suggested-action");
    gtk_widget_add_css_class(btn_reset, "destructive-action");
    gtk_widget_set_hexpand(btn_save, FALSE);
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_snippet_save), self);
    g_signal_connect(btn_reset, "clicked", G_CALLBACK(on_snippet_reset), self);
    g_signal_connect(btn_new, "clicked", G_CALLBACK(on_snippet_new), self);
    g_signal_connect(btn_remove, "clicked", G_CALLBACK(on_snippet_remove), self);

    gtk_box_append(GTK_BOX(toolbar), btn_save);
    gtk_box_append(GTK_BOX(toolbar), btn_reset);
    gtk_box_append(GTK_BOX(toolbar), btn_new);
    gtk_box_append(GTK_BOX(toolbar), btn_remove);

    GtkWidget *macrobar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_sel = gtk_button_new_with_label(_("Insert $SELECTED_TEXT"));
    GtkWidget *btn_file = gtk_button_new_with_label(_("Insert $FILENAME"));
    GtkWidget *btn_base = gtk_button_new_with_label(_("Insert $BASENAME"));
    g_object_set_data(G_OBJECT(btn_sel), "snippet-macro", "$SELECTED_TEXT");
    g_object_set_data(G_OBJECT(btn_file), "snippet-macro", "$FILENAME");
    g_object_set_data(G_OBJECT(btn_base), "snippet-macro", "$BASENAME");
    g_signal_connect(btn_sel, "clicked", G_CALLBACK(on_snippet_insert_macro), self);
    g_signal_connect(btn_file, "clicked", G_CALLBACK(on_snippet_insert_macro), self);
    g_signal_connect(btn_base, "clicked", G_CALLBACK(on_snippet_insert_macro), self);
    gtk_box_append(GTK_BOX(macrobar), btn_sel);
    gtk_box_append(GTK_BOX(macrobar), btn_file);
    gtk_box_append(GTK_BOX(macrobar), btn_base);

    /* ---- source view ---- */
    GtkSourceBuffer *sbuf = gtk_source_buffer_new(NULL);
    self->snippet_buf = GTK_TEXT_BUFFER(sbuf);

    GtkWidget *view = gtk_source_view_new_with_buffer(sbuf);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(view), TRUE);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(view), 4);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(view), TRUE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_widget_set_vexpand(view, TRUE);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolled, -1, 380);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), view);

    adw_preferences_group_add(grp_info, toolbar);
    adw_preferences_group_add(grp_info, macrobar);
    adw_preferences_group_add(grp_info, scrolled);
    adw_preferences_page_add(snip_page, grp_info);

    snippets_parse_file(self);
    self->current_snippet_index = 0;
    snippets_rebuild_combo(self);
    snippet_load_current_into_ui(self);
}

void silktex_prefs_present(SilktexPrefs *self, GtkWindow *parent)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    adw_dialog_present(ADW_DIALOG(self), GTK_WIDGET(parent));
}
