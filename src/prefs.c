/*
 * SilkTex - Preferences dialog
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "prefs.h"
#include "configfile.h"
#include "constants.h"
#include "style-schemes.h"
#include "utils.h"
#include "i18n.h"
#include <json-glib/json-glib.h>
#include <gtksourceview/gtksource.h>

struct _SilktexPrefs {
    AdwPreferencesDialog parent_instance;

    SilktexPrefsApplyFunc apply_func;
    gpointer apply_data;

    AdwSwitchRow *row_line_numbers;
    AdwSwitchRow *row_highlighting;
    AdwSwitchRow *row_textwrap;
    AdwSwitchRow *row_wordwrap;
    AdwSwitchRow *row_autoindent;
    AdwSwitchRow *row_spaces_tabs;
    AdwSpinRow *row_tabwidth;
    AdwComboRow *row_scheme_light;
    AdwComboRow *row_scheme_dark;

    AdwComboRow *row_typesetter;
    AdwSwitchRow *row_shellescape;
    AdwSwitchRow *row_synctex;
    AdwSwitchRow *row_auto_compile;
    AdwSpinRow *row_compile_timer;

    AdwSwitchRow *row_autosave;
    AdwSpinRow *row_autosave_timer;

    SilktexSnippets *snippets;
    AdwComboRow *row_snippet_mod1;
    AdwComboRow *row_snippet_mod2;
    GPtrArray *snippet_entries;
    GtkFlowBox *snippet_flow_box;
    char *snippet_search_query;  /* casefolded current query, or NULL */

    GtkStringList *scheme_ids_light;
    GtkStringList *scheme_ids_dark;
};

typedef struct {
    char *name;
    char *key;
    char *accel;
    char *body;
} SnippetEntry;

typedef struct {
    SilktexPrefs *prefs;
    AdwDialog *dialog;
    GtkStack *stack;
    GtkButton *btn_back;
    GtkButton *btn_next;
    GtkButton *btn_save;
    AdwEntryRow *name_row;
    AdwEntryRow *key_row;
    GtkTextBuffer *body_buf;
    AdwComboRow *mod1_row;
    AdwComboRow *mod2_row;
    AdwEntryRow *letter_row;
    int step;
    /* Overview / summary step */
    AdwEntryRow   *ov_name_row;
    AdwEntryRow   *ov_key_row;
    AdwEntryRow   *ov_accel_row;
    GtkTextBuffer *ov_body_buf;
    GtkDropDown   *mod1_dd;
    GtkDropDown   *mod2_dd;
} SnippetWizard;

static char *extract_accel_letter(const char *accel);
static void snippets_apply_modifiers(SilktexPrefs *self);
static void snippets_rebuild_pills(SilktexPrefs *self);
static char *display_to_body(const char *display);
static GtkWidget *make_tab_stop_bar(GtkTextBuffer *buf);

G_DEFINE_FINAL_TYPE (SilktexPrefs, silktex_prefs, ADW_TYPE_PREFERENCES_DIALOG)

    static void fire_apply(SilktexPrefs *self)
    {
        if (self->apply_func) self->apply_func(self->apply_data);
    }

static void ensure_snippet_editor_css(void)
{
    static gboolean installed = FALSE;
    if (installed) return;
    installed = TRUE;

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
                                      "scrolledwindow.silktex-snippet-scroller, "
                                      "scrolledwindow.silktex-snippet-scroller > viewport {"
                                      "  background-color: @window_bg_color;"
                                      "}"
                                      ".silktex-snippet-editor {"
                                      "  background-color: @window_bg_color;"
                                      "  color: @window_fg_color;"
                                      "  font-size: 18pt;"
                                      "  caret-color: @window_fg_color;"
                                      "}"
                                      "textview.silktex-snippet-editor text {"
                                      "  background-color: @window_bg_color;"
                                      "  color: @window_fg_color;"
                                      "  caret-color: @window_fg_color;"
                                      "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void setup_snippet_source_buffer(GtkSourceBuffer *buffer)
{
    if (!buffer) return;
    silktex_init_style_scheme_paths();
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage *lang = gtk_source_language_manager_get_language(lm, "latex");
    if (!lang) lang = gtk_source_language_manager_get_language(lm, "tex");
    if (lang) gtk_source_buffer_set_language(buffer, lang);
    GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
    const char *scheme_id = silktex_resolved_style_scheme_id();
    GtkSourceStyleScheme *scheme = (scheme_id && *scheme_id)
                                       ? gtk_source_style_scheme_manager_get_scheme(sm, scheme_id)
                                       : NULL;
    if (scheme) gtk_source_buffer_set_style_scheme(buffer, scheme);
    gtk_source_buffer_set_highlight_syntax(buffer, TRUE);
}

static void setup_snippet_source_view(GtkWidget *view)
{
    if (!view) return;
    ensure_snippet_editor_css();
    gtk_widget_add_css_class(view, "silktex-snippet-editor");
    gtk_widget_add_css_class(view, "view");
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 12);
    gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(view), 1);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(view), 1);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(view), TRUE);
}

static void setup_snippet_scroller(GtkWidget *scroller)
{
    if (!scroller) return;
    ensure_snippet_editor_css();
    gtk_widget_add_css_class(scroller, "silktex-snippet-scroller");
}

static void snippet_editor_update_height(GtkTextBuffer *buf, GtkWidget *scroller)
{
    if (!buf || !scroller) return;
    int lines = gtk_text_buffer_get_line_count(buf);
    if (lines < 1) lines = 1;
    int target = 20 + lines * 26;
    target = CLAMP(target, 80, 420);
    gtk_widget_set_size_request(scroller, -1, target);
}

static void on_snippet_editor_text_changed(GtkTextBuffer *buf, gpointer user_data)
{
    GtkWidget *scroller = GTK_WIDGET(user_data);
    snippet_editor_update_height(buf, scroller);
}

static gboolean scheme_is_dark(GtkSourceStyleScheme *scheme)
{
    GtkSourceStyle *style = gtk_source_style_scheme_get_style(scheme, "text");
    if (!style) return FALSE;

    char *bg = NULL;
    gboolean bg_set = FALSE;
    g_object_get(style, "background", &bg, "background-set", &bg_set, NULL);
    if (!bg_set || !bg) { g_free(bg); return FALSE; }

    GdkRGBA rgba;
    gboolean parsed = gdk_rgba_parse(&rgba, bg);
    g_free(bg);
    if (!parsed) return FALSE;

    double lum = 0.2126 * rgba.red + 0.7152 * rgba.green + 0.0722 * rgba.blue;
    return lum < 0.5;
}

static GtkStringList *build_scheme_model(SilktexPrefs *self, gboolean dark)
{
    silktex_init_style_scheme_paths();

    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();
    const char *const *ids = gtk_source_style_scheme_manager_get_scheme_ids(mgr);

    GtkStringList *names = gtk_string_list_new(NULL);
    GtkStringList **ids_out = dark ? &self->scheme_ids_dark : &self->scheme_ids_light;
    g_clear_object(ids_out);
    *ids_out = gtk_string_list_new(NULL);

    for (int i = 0; ids && ids[i]; i++) {
        GtkSourceStyleScheme *s = gtk_source_style_scheme_manager_get_scheme(mgr, ids[i]);
        if (scheme_is_dark(s) != dark) continue;
        gtk_string_list_append(names, gtk_source_style_scheme_get_name(s));
        gtk_string_list_append(*ids_out, ids[i]);
    }
    return names;
}

static int scheme_index_for_id(GtkStringList *list, const char *id)
{
    guint n = g_list_model_get_n_items(G_LIST_MODEL(list));
    for (guint i = 0; i < n; i++) {
        GtkStringObject *obj = g_list_model_get_item(G_LIST_MODEL(list), i);
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

static void snippets_parse_file(SilktexPrefs *self)
{
    g_clear_pointer(&self->snippet_entries, g_ptr_array_unref);
    self->snippet_entries = g_ptr_array_new_with_free_func(snippet_entry_free);

    if (!self->snippets) return;

    const char *fname = silktex_snippets_get_filename(self->snippets);
    g_autoptr(JsonParser) parser = json_parser_new();
    GError *error = NULL;
    if (!json_parser_load_from_file(parser, fname, &error)) {
        g_warning("Failed to parse snippets JSON: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) return;

    JsonObject *root_obj = json_node_get_object(root);
    g_autoptr(GList) members = json_object_get_members(root_obj);
    for (GList *l = members; l; l = l->next) {
        const char *member_name = l->data;
        JsonObject *obj = json_object_get_object_member(root_obj, member_name);
        if (!obj) continue;

        SnippetEntry *e = g_new0(SnippetEntry, 1);
        e->name = g_strdup(member_name);

        if (json_object_has_member(obj, "prefix")) {
            JsonNode *prefix_node = json_object_get_member(obj, "prefix");
            if (JSON_NODE_HOLDS_VALUE(prefix_node)) {
                e->key = g_strdup(json_node_get_string(prefix_node));
            } else if (JSON_NODE_HOLDS_ARRAY(prefix_node)) {
                JsonArray *arr = json_node_get_array(prefix_node);
                e->key = g_strdup(
                    json_array_get_length(arr) > 0 ? json_array_get_string_element(arr, 0) : "");
            }
        }
        if (!e->key) e->key = g_strdup("");

        e->accel = g_strdup(json_object_has_member(obj, "accelerator")
                                ? json_object_get_string_member(obj, "accelerator")
                                : "");

        if (json_object_has_member(obj, "description")) {
            g_free(e->name);
            const char *desc = json_object_get_string_member(obj, "description");
            e->name = g_strdup((desc && *desc) ? desc : member_name);
        }

        if (json_object_has_member(obj, "body")) {
            JsonNode *body_node = json_object_get_member(obj, "body");
            if (JSON_NODE_HOLDS_VALUE(body_node)) {
                e->body = g_strdup(json_node_get_string(body_node));
            } else if (JSON_NODE_HOLDS_ARRAY(body_node)) {
                JsonArray *arr = json_node_get_array(body_node);
                GString *body = g_string_new(NULL);
                guint len = json_array_get_length(arr);
                for (guint i = 0; i < len; i++) {
                    if (i > 0) g_string_append_c(body, '\n');
                    g_string_append(body, json_array_get_string_element(arr, i));
                }
                e->body = g_string_free(body, FALSE);
            }
        }
        if (!e->body) e->body = g_strdup("");

        g_ptr_array_add(self->snippet_entries, e);
    }
}

static void add_snippet_body_to_json(JsonBuilder *builder, const char *body)
{
    if (!body || !strchr(body, '\n')) {
        json_builder_add_string_value(builder, body ? body : "");
        return;
    }

    json_builder_begin_array(builder);
    gchar **lines = g_strsplit(body, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        json_builder_add_string_value(builder, lines[i]);
    }
    g_strfreev(lines);
    json_builder_end_array(builder);
}

static gboolean snippets_write_file(SilktexPrefs *self, GError **error)
{
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);

    for (guint i = 0; self->snippet_entries && i < self->snippet_entries->len; i++) {
        SnippetEntry *e = g_ptr_array_index(self->snippet_entries, i);
        if (!e->key || !e->name) continue;
        const char *name = (e->name && *e->name) ? e->name : e->key;

        json_builder_set_member_name(builder, name);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "prefix");
        json_builder_add_string_value(builder, e->key ? e->key : "");
        if (e->accel && *e->accel) {
            json_builder_set_member_name(builder, "accelerator");
            json_builder_add_string_value(builder, e->accel);
        }
        json_builder_set_member_name(builder, "body");
        add_snippet_body_to_json(builder, e->body);
        json_builder_set_member_name(builder, "description");
        json_builder_add_string_value(builder, name);
        json_builder_end_object(builder);
    }

    json_builder_end_object(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, TRUE);
    json_generator_set_indent(gen, 2);
    return json_generator_to_file(gen, silktex_snippets_get_filename(self->snippets), error);
}

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
static void on_scheme_light(AdwComboRow *r, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    guint idx = adw_combo_row_get_selected(r);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    GtkStringObject *obj = g_list_model_get_item(G_LIST_MODEL(self->scheme_ids_light), idx);
    if (!obj) return;
    config_set_string("Editor", "style_scheme_light", gtk_string_object_get_string(obj));
    g_object_unref(obj);
    fire_apply(self);
}

static void on_scheme_dark(AdwComboRow *r, GParamSpec *p, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    guint idx = adw_combo_row_get_selected(r);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    GtkStringObject *obj = g_list_model_get_item(G_LIST_MODEL(self->scheme_ids_dark), idx);
    if (!obj) return;
    config_set_string("Editor", "style_scheme_dark", gtk_string_object_get_string(obj));
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

static void silktex_prefs_init(SilktexPrefs *self)
{
    /* Initial dims — below the GNOME HIG 1024×600 baseline to fit small screens. */
    adw_dialog_set_content_width(ADW_DIALOG(self), 640);
    adw_dialog_set_content_height(ADW_DIALOG(self), 520);

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

    GtkStringList *scheme_names_light = build_scheme_model(self, FALSE);
    self->row_scheme_light = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_scheme_light),
                                  _("Light Theme Scheme"));
    adw_combo_row_set_model(self->row_scheme_light, G_LIST_MODEL(scheme_names_light));
    int si_light = scheme_index_for_id(self->scheme_ids_light,
                                       config_get_string("Editor", "style_scheme_light"));
    adw_combo_row_set_selected(self->row_scheme_light, (guint)si_light);
    g_signal_connect(self->row_scheme_light, "notify::selected", G_CALLBACK(on_scheme_light), self);

    GtkStringList *scheme_names_dark = build_scheme_model(self, TRUE);
    self->row_scheme_dark = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_scheme_dark),
                                  _("Dark Theme Scheme"));
    adw_combo_row_set_model(self->row_scheme_dark, G_LIST_MODEL(scheme_names_dark));
    int si_dark = scheme_index_for_id(self->scheme_ids_dark,
                                      config_get_string("Editor", "style_scheme_dark"));
    adw_combo_row_set_selected(self->row_scheme_dark, (guint)si_dark);
    g_signal_connect(self->row_scheme_dark, "notify::selected", G_CALLBACK(on_scheme_dark), self);

    adw_preferences_group_add(grp_scheme, GTK_WIDGET(self->row_scheme_light));
    adw_preferences_group_add(grp_scheme, GTK_WIDGET(self->row_scheme_dark));

    adw_preferences_page_add(editor_page, grp_display);
    adw_preferences_page_add(editor_page, grp_input);
    adw_preferences_page_add(editor_page, grp_scheme);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), editor_page);

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
    g_clear_object(&self->scheme_ids_light);
    g_clear_object(&self->scheme_ids_dark);
    g_clear_pointer(&self->snippet_entries, g_ptr_array_unref);
    g_clear_pointer(&self->snippet_search_query, g_free);
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

/* ── Import helpers ─────────────────────────────────────────────────────── */

/* Convert TeXstudio %<name%> / %| placeholders to $N / $0. */
static char *texstudio_tab_stops_to_vscode(const char *body)
{
    GString *out = g_string_new(NULL);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    int next_n = 1;
    const char *p = body;

    while (*p) {
        if (p[0] == '%' && p[1] == '|') {
            g_string_append(out, "$0");
            p += 2;
        } else if (p[0] == '%' && p[1] == '<') {
            const char *end = strstr(p + 2, "%>");
            if (!end) { g_string_append_c(out, *p++); continue; }
            /* Extract placeholder name (strip optional :flags after first ':') */
            gsize name_len = (gsize)(end - (p + 2));
            g_autofree char *raw = g_strndup(p + 2, name_len);
            char *colon = strchr(raw, ':');
            if (colon) *colon = '\0';
            g_autofree char *name = g_strdup(raw);

            gpointer stored = g_hash_table_lookup(seen, name);
            int n;
            if (stored) {
                n = GPOINTER_TO_INT(stored);
            } else {
                n = next_n++;
                g_hash_table_insert(seen, g_strdup(name), GINT_TO_POINTER(n));
            }
            g_string_append_printf(out, "${%d:%s}", n, name);
            p = end + 2;
        } else {
            g_string_append_c(out, *p++);
        }
    }
    g_hash_table_destroy(seen);
    return g_string_free(out, FALSE);
}

static void import_texstudio_macro(SilktexPrefs *self, const char *filename)
{
    if (!self->snippet_entries) return;

    g_autoptr(JsonParser) parser = json_parser_new();
    GError *err = NULL;
    if (!json_parser_load_from_file(parser, filename, &err)) {
        g_warning("Import: failed to parse %s: %s", filename, err ? err->message : "?");
        g_clear_error(&err);
        return;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) return;
    JsonObject *obj = json_node_get_object(root);

    const char *name   = json_object_has_member(obj, "name")   ? json_object_get_string_member(obj, "name")   : NULL;
    const char *abbrev = json_object_has_member(obj, "abbrev") ? json_object_get_string_member(obj, "abbrev") : NULL;

    const char *raw_body = NULL;
    if (json_object_has_member(obj, "tag")) {
        JsonNode *tag_node = json_object_get_member(obj, "tag");
        if (JSON_NODE_HOLDS_ARRAY(tag_node)) {
            JsonArray *arr = json_node_get_array(tag_node);
            if (json_array_get_length(arr) > 1)
                raw_body = json_array_get_string_element(arr, 1);
        }
    }
    if (!raw_body || !*raw_body) return;

    SnippetEntry *e = g_new0(SnippetEntry, 1);
    e->name  = g_strdup((name && *name) ? name : (abbrev && *abbrev ? abbrev : "Imported"));
    e->key   = g_strdup((abbrev && *abbrev) ? abbrev : "");
    e->accel = g_strdup("");
    e->body  = texstudio_tab_stops_to_vscode(raw_body);
    g_ptr_array_add(self->snippet_entries, e);
}

static void import_gummi_snippets(SilktexPrefs *self, const char *filename)
{
    if (!self->snippet_entries) return;

    g_autofree char *contents = NULL;
    GError *err = NULL;
    if (!g_file_get_contents(filename, &contents, NULL, &err)) {
        g_warning("Import: cannot read %s: %s", filename, err ? err->message : "?");
        g_clear_error(&err);
        return;
    }

    gchar **lines = g_strsplit(contents, "\n", -1);
    int i = 0;
    while (lines[i]) {
        /* "snippet trigger,,description" or "snippet trigger" */
        if (g_str_has_prefix(lines[i], "snippet ")) {
            const char *rest = lines[i] + 8;
            const char *sep  = strstr(rest, ",,");
            g_autofree char *trigger = sep ? g_strndup(rest, (gsize)(sep - rest)) : g_strdup(rest);
            const char *description  = sep ? sep + 2 : trigger;
            g_strstrip(trigger);

            GString *body = g_string_new(NULL);
            i++;
            while (lines[i] && (lines[i][0] == '\t' || lines[i][0] == ' ')) {
                const char *line = lines[i];
                if (line[0] == '\t') line++;  /* strip one leading tab */
                if (body->len > 0) g_string_append_c(body, '\n');
                g_string_append(body, line);
                i++;
            }

            if (body->len > 0) {
                SnippetEntry *e = g_new0(SnippetEntry, 1);
                e->name  = g_strdup(description && *description ? description : trigger);
                e->key   = g_strdup(trigger);
                e->accel = g_strdup("");
                e->body  = g_string_free(body, FALSE);
                g_ptr_array_add(self->snippet_entries, e);
            } else {
                g_string_free(body, TRUE);
            }
        } else {
            i++;
        }
    }
    g_strfreev(lines);
}

static void on_import_file_chosen(GObject *src, GAsyncResult *res, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
    if (!file) { g_clear_error(&err); return; }

    g_autofree char *path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) return;

    if (g_str_has_suffix(path, ".txsMacro"))
        import_texstudio_macro(self, path);
    else  /* .cfg or anything else → try Gummi */
        import_gummi_snippets(self, path);

    snippets_rebuild_pills(self);
}

static void on_snippet_import(GtkButton *btn, gpointer ud)
{
    (void)btn;
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));

    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Import Snippets"));

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *f_gummi = gtk_file_filter_new();
    gtk_file_filter_set_name(f_gummi, _("Gummi snippets (*.cfg)"));
    gtk_file_filter_add_pattern(f_gummi, "*.cfg");
    g_list_store_append(filters, f_gummi);
    g_object_unref(f_gummi);

    GtkFileFilter *f_txs = gtk_file_filter_new();
    gtk_file_filter_set_name(f_txs, _("TeXstudio macro (*.txsMacro)"));
    gtk_file_filter_add_pattern(f_txs, "*.txsMacro");
    g_list_store_append(filters, f_txs);
    g_object_unref(f_txs);

    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
    g_object_unref(filters);

    gtk_file_dialog_open(dlg, GTK_WINDOW(root), NULL, on_import_file_chosen, self);
    g_object_unref(dlg);
}

/* ── End import helpers ─────────────────────────────────────────────────── */

static void on_snippet_save(GtkButton *btn, gpointer ud)
{
    (void)btn;
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (!self->snippets || !self->snippet_entries) return;
    GError *err = NULL;
    if (!snippets_write_file(self, &err)) {
        g_warning("Failed to save snippets: %s", err->message);
        g_error_free(err);
    } else {
        silktex_snippets_reload(self->snippets);
    }
}

static void on_snippet_reset_response(AdwAlertDialog *dialog, const char *response, gpointer ud)
{
    (void)dialog;
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (g_strcmp0(response, "reset") != 0) return;
    silktex_snippets_reset_to_default(self->snippets);
    snippets_parse_file(self);
    snippets_rebuild_pills(self);
}

static void on_snippet_reset(GtkButton *btn, gpointer ud)
{
    (void)btn;
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (!self->snippets) return;
    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Reset snippets?"), _("This replaces all custom snippets with defaults.")));
    adw_alert_dialog_add_response(dlg, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dlg, "reset", _("Reset"));
    adw_alert_dialog_set_response_appearance(dlg, "reset", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_snippet_reset_response), self);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(self));
}

/* Each snippet stores a single letter; the two global modifier keys chosen here
 * combine with it at runtime to form the full shortcut. */

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

static const char *wizard_step_names[]   = {"identity", "command", "shortcut", "overview"};
static const char *wizard_step_titles[]  = {
    N_("Name and Trigger"), N_("Snippet"), N_("Shortcut"), N_("Save")
};

static char *build_accel_from_parts(guint mod1_idx, guint mod2_idx, const char *letter)
{
    GString *s = g_string_new(NULL);
    if (mod1_idx < G_N_ELEMENTS(MODIFIER_CHOICES) && MODIFIER_CHOICES[mod1_idx].config[0] != '\0')
        g_string_append_printf(s, "<%s>", MODIFIER_CHOICES[mod1_idx].config);
    if (mod2_idx < G_N_ELEMENTS(MODIFIER_CHOICES) && MODIFIER_CHOICES[mod2_idx].config[0] != '\0')
        g_string_append_printf(s, "<%s>", MODIFIER_CHOICES[mod2_idx].config);
    if (letter && *letter) g_string_append(s, letter);
    return g_string_free(s, FALSE);
}

static void snippet_wizard_update_overview(SnippetWizard *w)
{
    const char *name = gtk_editable_get_text(GTK_EDITABLE(w->name_row));
    const char *key = gtk_editable_get_text(GTK_EDITABLE(w->key_row));
    const char *letter = gtk_editable_get_text(GTK_EDITABLE(w->letter_row));
    guint m1 = gtk_drop_down_get_selected(w->mod1_dd);
    guint m2 = gtk_drop_down_get_selected(w->mod2_dd);
    g_autofree char *accel = build_accel_from_parts(m1, m2, letter);

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(w->body_buf, &s, &e);
    g_autofree char *body = gtk_text_buffer_get_text(w->body_buf, &s, &e, FALSE);

    gtk_editable_set_text(GTK_EDITABLE(w->ov_name_row), name ? name : "");
    gtk_editable_set_text(GTK_EDITABLE(w->ov_key_row), key ? key : "");
    gtk_editable_set_text(GTK_EDITABLE(w->ov_accel_row), accel ? accel : "");
    gtk_text_buffer_set_text(w->ov_body_buf, body ? body : "", -1);
}

static void snippet_wizard_set_step(SnippetWizard *w, int step)
{
    w->step = CLAMP(step, 0, 3);
    gtk_stack_set_visible_child_name(w->stack, wizard_step_names[w->step]);
    adw_dialog_set_title(w->dialog, _(wizard_step_titles[w->step]));
    gtk_widget_set_sensitive(GTK_WIDGET(w->btn_back), w->step > 0);
    gtk_widget_set_visible(GTK_WIDGET(w->btn_next), w->step < 3);
    gtk_widget_set_visible(GTK_WIDGET(w->btn_save), w->step == 3);
    if (w->step == 3) snippet_wizard_update_overview(w);
}

static void snippet_wizard_free(gpointer data)
{
    g_free(data);
}

static void on_snippet_wizard_back(GtkButton *btn, gpointer ud)
{
    SnippetWizard *w = ud;
    snippet_wizard_set_step(w, w->step - 1);
}

static void on_snippet_wizard_next(GtkButton *btn, gpointer ud)
{
    SnippetWizard *w = ud;
    snippet_wizard_set_step(w, w->step + 1);
}

static void on_snippet_wizard_discard(GtkButton *btn, gpointer ud)
{
    (void)btn;
    AdwDialog *dialog = ADW_DIALOG(ud);
    adw_dialog_close(dialog);
}

static void on_snippet_wizard_save(GtkButton *btn, gpointer ud)
{
    (void)btn;
    SnippetWizard *w = ud;
    SilktexPrefs *self = w->prefs;

    SnippetEntry *e = g_new0(SnippetEntry, 1);
    e->name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(w->ov_name_row)));
    e->key = g_strdup(gtk_editable_get_text(GTK_EDITABLE(w->ov_key_row)));
    e->accel = g_strdup(gtk_editable_get_text(GTK_EDITABLE(w->ov_accel_row)));
    GtkTextIter s, t;
    gtk_text_buffer_get_bounds(w->ov_body_buf, &s, &t);
    g_autofree char *body_disp = gtk_text_buffer_get_text(w->ov_body_buf, &s, &t, FALSE);
    e->body = display_to_body(body_disp);

    if (!e->name || !*e->name) {
        g_free(e->name);
        e->name = g_strdup(_("New Snippet"));
    }
    if (!e->key) e->key = g_strdup("");
    if (!e->accel) e->accel = g_strdup("");
    if (!e->body) e->body = g_strdup("");

    g_ptr_array_add(self->snippet_entries, e);
    snippets_rebuild_pills(self);
    adw_dialog_close(w->dialog);
}

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

static void snippets_apply_modifiers(SilktexPrefs *self)
{
    if (!self->snippets) return;
    const char *m1 = config_get_string("Snippets", "modifier1");
    const char *m2 = config_get_string("Snippets", "modifier2");
    silktex_snippets_set_modifiers(self->snippets, m1, m2);
}

static void on_snippet_modifier_changed(AdwComboRow *row, GParamSpec *p, gpointer ud)
{
    (void)row;
    (void)p;
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    guint i1 = adw_combo_row_get_selected(self->row_snippet_mod1);
    guint i2 = adw_combo_row_get_selected(self->row_snippet_mod2);
    config_set_string("Snippets", "modifier1", modifier_choice_config(i1));
    config_set_string("Snippets", "modifier2", modifier_choice_config(i2));
    snippets_apply_modifiers(self);
    fire_apply(self);
}

static void on_snippet_new(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    SnippetWizard *w = g_new0(SnippetWizard, 1);
    w->prefs = self;
    w->dialog = adw_dialog_new();
    adw_dialog_set_title(w->dialog, _("New Snippet"));
    adw_dialog_set_content_width(w->dialog, 620);
    adw_dialog_set_content_height(w->dialog, 520);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    w->stack = GTK_STACK(gtk_stack_new());
    gtk_widget_set_vexpand(GTK_WIDGET(w->stack), TRUE);
    gtk_stack_set_transition_type(w->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

    /* Step 0 — Name and Trigger */
    GtkWidget *p1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    w->name_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->name_row), _("Shortcut Name"));
    w->key_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->key_row), _("Tab Trigger"));
    gtk_box_append(GTK_BOX(p1), GTK_WIDGET(w->name_row));
    gtk_box_append(GTK_BOX(p1), GTK_WIDGET(w->key_row));

    /* Step 1 — Snippet body */
    GtkWidget *p2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *body_desc = gtk_label_new(
        _("Write the snippet body. Use the • button to insert tab stops — "
          "they will be visited in order when you expand the snippet."));
    gtk_label_set_wrap(GTK_LABEL(body_desc), TRUE);
    gtk_widget_set_halign(body_desc, GTK_ALIGN_START);
    gtk_widget_add_css_class(body_desc, "dim-label");
    GtkSourceBuffer *body_buf = gtk_source_buffer_new(NULL);
    setup_snippet_source_buffer(body_buf);
    w->body_buf = GTK_TEXT_BUFFER(body_buf);
    GtkWidget *body_view = gtk_source_view_new_with_buffer(body_buf);
    setup_snippet_source_view(body_view);
    gtk_widget_set_vexpand(body_view, TRUE);
    GtkWidget *body_scrolled = gtk_scrolled_window_new();
    setup_snippet_scroller(body_scrolled);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(body_scrolled), body_view);
    g_signal_connect(body_buf, "changed", G_CALLBACK(on_snippet_editor_text_changed), body_scrolled);
    snippet_editor_update_height(GTK_TEXT_BUFFER(body_buf), body_scrolled);
    gtk_box_append(GTK_BOX(p2), body_desc);
    gtk_box_append(GTK_BOX(p2), make_tab_stop_bar(GTK_TEXT_BUFFER(body_buf)));
    gtk_box_append(GTK_BOX(p2), body_scrolled);

    /* Step 2 — Shortcut (modifiers + letter) using AdwComboRow for consistency */
    GtkWidget *p3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    AdwPreferencesGroup *grp_sc = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_sc, _("Modifiers"));
    w->mod1_dd = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(build_modifier_model()), NULL));
    w->mod2_dd = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(build_modifier_model()), NULL));
    gtk_drop_down_set_selected(w->mod1_dd,
        modifier_choice_index_for(config_get_string("Snippets", "modifier1")));
    gtk_drop_down_set_selected(w->mod2_dd,
        modifier_choice_index_for(config_get_string("Snippets", "modifier2")));
    /* Wrap the two drop-downs in an AdwActionRow each for consistent look */
    AdwActionRow *mod1_ar = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mod1_ar), _("Modifier 1"));
    adw_action_row_add_suffix(mod1_ar, GTK_WIDGET(w->mod1_dd));
    gtk_widget_set_valign(GTK_WIDGET(w->mod1_dd), GTK_ALIGN_CENTER);
    AdwActionRow *mod2_ar = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mod2_ar), _("Modifier 2"));
    adw_action_row_add_suffix(mod2_ar, GTK_WIDGET(w->mod2_dd));
    gtk_widget_set_valign(GTK_WIDGET(w->mod2_dd), GTK_ALIGN_CENTER);
    adw_preferences_group_add(grp_sc, GTK_WIDGET(mod1_ar));
    adw_preferences_group_add(grp_sc, GTK_WIDGET(mod2_ar));
    w->letter_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->letter_row), _("Letter"));
    gtk_box_append(GTK_BOX(p3), GTK_WIDGET(grp_sc));
    gtk_box_append(GTK_BOX(p3), GTK_WIDGET(w->letter_row));

    /* Step 3 — Overview / Save */
    GtkWidget *p4 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    /* Discard + Save at the top of this page */
    GtkWidget *p4_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *p4_discard = gtk_button_new_with_label(_("Discard"));
    gtk_widget_add_css_class(p4_discard, "destructive-action");
    g_signal_connect(p4_discard, "clicked", G_CALLBACK(on_snippet_wizard_discard), w->dialog);
    GtkWidget *p4_spacer = gtk_label_new(NULL);
    gtk_widget_set_hexpand(p4_spacer, TRUE);
    gtk_box_append(GTK_BOX(p4_top), p4_discard);
    gtk_box_append(GTK_BOX(p4_top), p4_spacer);
    gtk_box_append(GTK_BOX(p4), p4_top);

    w->ov_name_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->ov_name_row), _("Name"));
    w->ov_key_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->ov_key_row), _("Tab Trigger"));
    w->ov_accel_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(w->ov_accel_row), _("Shortcut"));
    GtkSourceBuffer *ov_buf = gtk_source_buffer_new(NULL);
    setup_snippet_source_buffer(ov_buf);
    w->ov_body_buf = GTK_TEXT_BUFFER(ov_buf);
    GtkWidget *ov_view = gtk_source_view_new_with_buffer(ov_buf);
    setup_snippet_source_view(ov_view);
    gtk_widget_set_vexpand(ov_view, TRUE);
    GtkWidget *ov_scrolled = gtk_scrolled_window_new();
    setup_snippet_scroller(ov_scrolled);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ov_scrolled), ov_view);
    g_signal_connect(ov_buf, "changed", G_CALLBACK(on_snippet_editor_text_changed), ov_scrolled);
    snippet_editor_update_height(GTK_TEXT_BUFFER(ov_buf), ov_scrolled);
    gtk_box_append(GTK_BOX(p4), GTK_WIDGET(w->ov_name_row));
    gtk_box_append(GTK_BOX(p4), GTK_WIDGET(w->ov_key_row));
    gtk_box_append(GTK_BOX(p4), GTK_WIDGET(w->ov_accel_row));
    gtk_box_append(GTK_BOX(p4), ov_scrolled);

    gtk_stack_add_named(w->stack, p1, "identity");
    gtk_stack_add_named(w->stack, p2, "command");
    gtk_stack_add_named(w->stack, p3, "shortcut");
    gtk_stack_add_named(w->stack, p4, "overview");
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(w->stack));

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    w->btn_back = GTK_BUTTON(gtk_button_new_with_label(_("Back")));
    w->btn_next = GTK_BUTTON(gtk_button_new_with_label(_("Next")));
    w->btn_save = GTK_BUTTON(gtk_button_new_with_label(_("Save")));
    gtk_widget_add_css_class(GTK_WIDGET(w->btn_save), "suggested-action");
    g_signal_connect(w->btn_back, "clicked", G_CALLBACK(on_snippet_wizard_back), w);
    g_signal_connect(w->btn_next, "clicked", G_CALLBACK(on_snippet_wizard_next), w);
    g_signal_connect(w->btn_save, "clicked", G_CALLBACK(on_snippet_wizard_save), w);
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(w->btn_back));
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(w->btn_next));
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(w->btn_save));
    gtk_box_append(GTK_BOX(root), actions);

    adw_dialog_set_child(w->dialog, root);
    g_object_set_data_full(G_OBJECT(w->dialog), "snippet-wizard", w, snippet_wizard_free);
    snippet_wizard_set_step(w, 0);
    adw_dialog_present(w->dialog, GTK_WIDGET(self));
}

static void on_snippet_remove_at(SilktexPrefs *self, guint idx)
{
    if (!self->snippet_entries || idx >= self->snippet_entries->len) return;
    g_ptr_array_remove_index(self->snippet_entries, idx);
    snippets_rebuild_pills(self);
}

/* ------------------------------------------------------------------ */
/* Edit-dialog for an existing snippet pill                            */
/* ------------------------------------------------------------------ */

typedef struct {
    SilktexPrefs *prefs;
    AdwDialog    *dialog;
    guint         idx;
    char         *orig_name;
    char         *orig_key;
    char         *orig_body;
    char         *orig_accel;
} SnippetEditCtx;

static void snippet_edit_ctx_free(gpointer data)
{
    SnippetEditCtx *ctx = data;
    g_free(ctx->orig_name);
    g_free(ctx->orig_key);
    g_free(ctx->orig_body);
    g_free(ctx->orig_accel);
    g_free(ctx);
}

static void on_edit_name_changed(AdwEntryRow *row, GParamSpec *p, gpointer ud)
{
    (void)p;
    SnippetEditCtx *ctx = ud;
    if (!ctx->prefs->snippet_entries || ctx->idx >= ctx->prefs->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(ctx->prefs->snippet_entries, ctx->idx);
    g_free(e->name);
    e->name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(row)));
    adw_dialog_set_title(ctx->dialog, e->name && *e->name ? e->name : _("Snippet"));
    snippets_rebuild_pills(ctx->prefs);
}

static void on_edit_key_changed(AdwEntryRow *row, GParamSpec *p, gpointer ud)
{
    (void)p;
    SnippetEditCtx *ctx = ud;
    if (!ctx->prefs->snippet_entries || ctx->idx >= ctx->prefs->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(ctx->prefs->snippet_entries, ctx->idx);
    g_free(e->key);
    e->key = g_strdup(gtk_editable_get_text(GTK_EDITABLE(row)));
}

static void on_edit_body_changed(GtkTextBuffer *buf, gpointer ud)
{
    SnippetEditCtx *ctx = ud;
    if (!ctx->prefs->snippet_entries || ctx->idx >= ctx->prefs->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(ctx->prefs->snippet_entries, ctx->idx);
    GtkTextIter s, t;
    gtk_text_buffer_get_bounds(buf, &s, &t);
    g_autofree char *disp = gtk_text_buffer_get_text(buf, &s, &t, FALSE);
    g_free(e->body);
    e->body = display_to_body(disp);
}

static void rebuild_edit_accel(SnippetEditCtx *ctx, GtkDropDown *mod1, GtkDropDown *mod2,
                                AdwEntryRow *letter_row)
{
    if (!ctx->prefs->snippet_entries || ctx->idx >= ctx->prefs->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(ctx->prefs->snippet_entries, ctx->idx);
    guint m1 = gtk_drop_down_get_selected(mod1);
    guint m2 = gtk_drop_down_get_selected(mod2);
    const char *letter = gtk_editable_get_text(GTK_EDITABLE(letter_row));
    g_free(e->accel);
    e->accel = build_accel_from_parts(m1, m2, letter);
}

static void on_edit_mod_changed(GtkDropDown *dd, GParamSpec *p, gpointer ud)
{
    (void)dd; (void)p;
    gpointer *pack = ud;
    rebuild_edit_accel(pack[0], pack[1], pack[2], pack[3]);
}

static void on_edit_letter_changed(AdwEntryRow *row, GParamSpec *p, gpointer ud)
{
    (void)row; (void)p;
    gpointer *pack = ud;
    rebuild_edit_accel(pack[0], pack[1], pack[2], pack[3]);
}

static void on_snippet_edit_delete_confirm(AdwAlertDialog *dlg, const char *response, gpointer ud)
{
    if (g_strcmp0(response, "delete") != 0) return;
    gpointer *pack = ud;
    SnippetEditCtx *ctx = pack[0];
    on_snippet_remove_at(ctx->prefs, ctx->idx);
    adw_dialog_close(ctx->dialog);
}

static void on_snippet_edit_delete(GtkButton *btn, gpointer ud)
{
    (void)btn;
    SnippetEditCtx *ctx = ud;
    if (!ctx->prefs->snippet_entries || ctx->idx >= ctx->prefs->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(ctx->prefs->snippet_entries, ctx->idx);

    AdwAlertDialog *confirm = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Delete Snippet?"),
        e->name && *e->name ? e->name : _("This snippet")));
    adw_alert_dialog_add_responses(confirm, "cancel", _("Cancel"), "delete", _("Delete"), NULL);
    adw_alert_dialog_set_response_appearance(confirm, "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(confirm, "cancel");

    /* Pack ctx as a single pointer — it stays alive as long as the edit dialog is open */
    static gpointer pack[1];
    pack[0] = ctx;
    g_signal_connect(confirm, "response", G_CALLBACK(on_snippet_edit_delete_confirm), pack);
    adw_dialog_present(ADW_DIALOG(confirm), GTK_WIDGET(ctx->dialog));
}

static void on_snippet_edit_discard(GtkButton *btn, gpointer ud)
{
    (void)btn;
    SnippetEditCtx *ctx = ud;
    if (!ctx->prefs->snippet_entries || ctx->idx >= ctx->prefs->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(ctx->prefs->snippet_entries, ctx->idx);
    g_free(e->name);  e->name  = g_strdup(ctx->orig_name);
    g_free(e->key);   e->key   = g_strdup(ctx->orig_key);
    g_free(e->body);  e->body  = g_strdup(ctx->orig_body);
    g_free(e->accel); e->accel = g_strdup(ctx->orig_accel);
    snippets_rebuild_pills(ctx->prefs);
    adw_dialog_close(ctx->dialog);
}

/* U+2022 BULLET used as a display stand-in for $N tab stops in the body editor. */
#define TAB_STOP_BULLET "\xe2\x97\x8f"  /* U+25CF BLACK CIRCLE */

/* Replace $0, $1, … with • for display in the body editor. */
static char *body_to_display(const char *body)
{
    if (!body) return g_strdup("");
    GString *out = g_string_new(NULL);
    const char *p = body;
    while (*p) {
        if (*p == '$') {
            const char *q = p + 1;
            while (*q >= '0' && *q <= '9') q++;
            if (q > p + 1) {
                g_string_append(out, TAB_STOP_BULLET);
                p = q;
                continue;
            }
        }
        const char *next = g_utf8_next_char(p);
        g_string_append_len(out, p, next - p);
        p = next;
    }
    return g_string_free(out, FALSE);
}

/* Replace each • with $1, $2, … in order for storage. */
static char *display_to_body(const char *display)
{
    if (!display) return g_strdup("");
    GString *out = g_string_new(NULL);
    int n = 1;
    const char *p = display;
    while (*p) {
        gunichar c = g_utf8_get_char(p);
        if (c == 0x25CF) {
            g_string_append_printf(out, "$%d", n++);
            p = g_utf8_next_char(p);
        } else {
            const char *next = g_utf8_next_char(p);
            g_string_append_len(out, p, next - p);
            p = next;
        }
    }
    return g_string_free(out, FALSE);
}

static void on_insert_bullet(GtkButton *btn, gpointer ud)
{
    (void)btn;
    gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(ud), TAB_STOP_BULLET, -1);
}

static GtkWidget *make_tab_stop_bar(GtkTextBuffer *buf)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(bar, 4);
    gtk_widget_set_margin_bottom(bar, 2);
    GtkWidget *lbl = gtk_label_new(_("Tab stop:"));
    gtk_widget_add_css_class(lbl, "dim-label");
    GtkWidget *btn = gtk_button_new();
    gtk_widget_add_css_class(btn, "flat");
    gtk_widget_set_tooltip_text(btn, _("Insert a tab stop (●1, ●2, … in order)"));
    GtkWidget *btn_lbl = gtk_label_new(TAB_STOP_BULLET);
    gtk_widget_add_css_class(btn_lbl, "monospace");
    gtk_button_set_child(GTK_BUTTON(btn), btn_lbl);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_insert_bullet), buf);
    gtk_box_append(GTK_BOX(bar), lbl);
    gtk_box_append(GTK_BOX(bar), btn);
    return bar;
}

static void snippet_edit_dialog_show(SilktexPrefs *self, guint idx)
{
    if (!self->snippet_entries || idx >= self->snippet_entries->len) return;
    SnippetEntry *e = g_ptr_array_index(self->snippet_entries, idx);

    SnippetEditCtx *ctx = g_new0(SnippetEditCtx, 1);
    ctx->prefs      = self;
    ctx->idx        = idx;
    ctx->orig_name  = g_strdup(e->name);
    ctx->orig_key   = g_strdup(e->key);
    ctx->orig_body  = g_strdup(e->body);
    ctx->orig_accel = g_strdup(e->accel);

    AdwDialog *dlg = adw_dialog_new();
    ctx->dialog = dlg;
    adw_dialog_set_title(dlg, e->name && *e->name ? e->name : _("Snippet"));
    adw_dialog_set_content_width(dlg, 500);
    adw_dialog_set_content_height(dlg, 580);
    g_object_set_data_full(G_OBJECT(dlg), "edit-ctx", ctx, snippet_edit_ctx_free);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 8);
    gtk_widget_set_margin_bottom(root, 12);

    /* Fields */
    AdwPreferencesGroup *grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());

    AdwEntryRow *name_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(name_row), _("Name"));
    gtk_editable_set_text(GTK_EDITABLE(name_row), e->name ? e->name : "");
    g_signal_connect(name_row, "notify::text", G_CALLBACK(on_edit_name_changed), ctx);

    AdwEntryRow *key_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(key_row), _("Tab Trigger"));
    gtk_editable_set_text(GTK_EDITABLE(key_row), e->key ? e->key : "");
    g_signal_connect(key_row, "notify::text", G_CALLBACK(on_edit_key_changed), ctx);

    /* Parse existing accel to pre-fill modifiers + letter */
    g_autofree char *letter_str = extract_accel_letter(e->accel);
    guint m1_idx = 0, m2_idx = 0;
    if (e->accel) {
        /* Find up to two <Mod> prefixes */
        const char *p = e->accel;
        guint *slots[] = {&m1_idx, &m2_idx};
        int slot = 0;
        while (*p == '<' && slot < 2) {
            const char *close = strchr(p, '>');
            if (!close) break;
            char *mod = g_strndup(p + 1, close - p - 1);
            for (guint i = 0; i < G_N_ELEMENTS(MODIFIER_CHOICES); i++) {
                if (g_ascii_strcasecmp(mod, MODIFIER_CHOICES[i].config) == 0)
                    *slots[slot] = i;
            }
            g_free(mod);
            p = close + 1;
            slot++;
        }
    }

    GtkDropDown *mod1_dd = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(build_modifier_model()), NULL));
    GtkDropDown *mod2_dd = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(build_modifier_model()), NULL));
    gtk_drop_down_set_selected(mod1_dd, m1_idx);
    gtk_drop_down_set_selected(mod2_dd, m2_idx);

    AdwEntryRow *letter_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(letter_row), _("Shortcut Letter"));
    gtk_editable_set_text(GTK_EDITABLE(letter_row), letter_str);

    /* Signal pack for mod+letter → accel rebuild */
    gpointer *mod_pack = g_new(gpointer, 4);
    mod_pack[0] = ctx;
    mod_pack[1] = mod1_dd;
    mod_pack[2] = mod2_dd;
    mod_pack[3] = letter_row;
    g_object_set_data_full(G_OBJECT(dlg), "mod-pack", mod_pack, g_free);
    g_signal_connect(mod1_dd, "notify::selected", G_CALLBACK(on_edit_mod_changed), mod_pack);
    g_signal_connect(mod2_dd, "notify::selected", G_CALLBACK(on_edit_mod_changed), mod_pack);
    g_signal_connect(letter_row, "notify::text", G_CALLBACK(on_edit_letter_changed), mod_pack);

    AdwActionRow *mod1_ar = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mod1_ar), _("Modifier 1"));
    adw_action_row_add_suffix(mod1_ar, GTK_WIDGET(mod1_dd));
    gtk_widget_set_valign(GTK_WIDGET(mod1_dd), GTK_ALIGN_CENTER);
    AdwActionRow *mod2_ar = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mod2_ar), _("Modifier 2"));
    adw_action_row_add_suffix(mod2_ar, GTK_WIDGET(mod2_dd));
    gtk_widget_set_valign(GTK_WIDGET(mod2_dd), GTK_ALIGN_CENTER);

    adw_preferences_group_add(grp, GTK_WIDGET(name_row));
    adw_preferences_group_add(grp, GTK_WIDGET(key_row));
    adw_preferences_group_add(grp, GTK_WIDGET(mod1_ar));
    adw_preferences_group_add(grp, GTK_WIDGET(mod2_ar));
    adw_preferences_group_add(grp, GTK_WIDGET(letter_row));
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(grp));

    /* Body editor */
    AdwPreferencesGroup *body_grp = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(body_grp, _("Snippet Body"));
    GtkSourceBuffer *sbuf = gtk_source_buffer_new(NULL);
    setup_snippet_source_buffer(sbuf);
    g_autofree char *body_disp = body_to_display(e->body ? e->body : "");
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sbuf), body_disp, -1);
    g_signal_connect(sbuf, "changed", G_CALLBACK(on_edit_body_changed), ctx);
    GtkWidget *bview = gtk_source_view_new_with_buffer(sbuf);
    setup_snippet_source_view(bview);
    gtk_widget_set_vexpand(bview, TRUE);
    GtkWidget *bscroll = gtk_scrolled_window_new();
    setup_snippet_scroller(bscroll);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(bscroll), bview);
    g_signal_connect(sbuf, "changed", G_CALLBACK(on_snippet_editor_text_changed), bscroll);
    snippet_editor_update_height(GTK_TEXT_BUFFER(sbuf), bscroll);
    adw_preferences_group_add(body_grp, make_tab_stop_bar(GTK_TEXT_BUFFER(sbuf)));
    adw_preferences_group_add(body_grp, bscroll);
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(body_grp));

    /* Bottom action bar: [Delete] ··· [Discard] [Done] */
    GtkWidget *bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_del = gtk_button_new_with_label(_("Delete"));
    gtk_widget_add_css_class(btn_del, "destructive-action");
    g_signal_connect(btn_del, "clicked", G_CALLBACK(on_snippet_edit_delete), ctx);
    GtkWidget *spacer = gtk_label_new(NULL);
    gtk_widget_set_hexpand(spacer, TRUE);
    GtkWidget *btn_discard = gtk_button_new_with_label(_("Discard"));
    g_signal_connect(btn_discard, "clicked", G_CALLBACK(on_snippet_edit_discard), ctx);
    GtkWidget *btn_done = gtk_button_new_with_label(_("Done"));
    gtk_widget_add_css_class(btn_done, "suggested-action");
    g_signal_connect_swapped(btn_done, "clicked", G_CALLBACK(adw_dialog_close), dlg);
    gtk_box_append(GTK_BOX(bottom_bar), btn_del);
    gtk_box_append(GTK_BOX(bottom_bar), spacer);
    gtk_box_append(GTK_BOX(bottom_bar), btn_discard);
    gtk_box_append(GTK_BOX(bottom_bar), btn_done);
    gtk_box_append(GTK_BOX(root), bottom_bar);

    adw_dialog_set_child(dlg, root);
    adw_dialog_present(dlg, GTK_WIDGET(self));
}

/* ------------------------------------------------------------------ */
/* Pill widget + flow box                                              */
/* ------------------------------------------------------------------ */

static void on_snippet_pill_clicked(GtkButton *btn, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "snippet-idx"));
    snippet_edit_dialog_show(self, idx);
}

static gboolean snippet_pill_filter(GtkFlowBoxChild *child, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    if (!self->snippet_search_query || !*self->snippet_search_query) return TRUE;

    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(child), "snippet-idx"));
    if (!self->snippet_entries || idx >= self->snippet_entries->len) return FALSE;
    SnippetEntry *e = g_ptr_array_index(self->snippet_entries, idx);

    const char *fields[] = { e->name, e->key, e->body };
    for (gsize i = 0; i < G_N_ELEMENTS(fields); i++) {
        if (!fields[i]) continue;
        g_autofree char *fold = g_utf8_casefold(fields[i], -1);
        if (strstr(fold, self->snippet_search_query)) return TRUE;
    }
    return FALSE;
}

static void on_snippet_search_changed(GtkEditable *entry, gpointer ud)
{
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    const char *q = gtk_editable_get_text(entry);
    g_clear_pointer(&self->snippet_search_query, g_free);
    if (q && *q) self->snippet_search_query = g_utf8_casefold(q, -1);
    if (self->snippet_flow_box)
        gtk_flow_box_invalidate_filter(self->snippet_flow_box);
}

static void snippets_rebuild_pills(SilktexPrefs *self)
{
    if (!self->snippet_flow_box) return;

    /* Clear all children */
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(self->snippet_flow_box));
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(self->snippet_flow_box, child);
        child = next;
    }

    if (!self->snippet_entries) return;

    for (guint i = 0; i < self->snippet_entries->len; i++) {
        SnippetEntry *e = g_ptr_array_index(self->snippet_entries, i);
        GtkWidget *pill = gtk_button_new_with_label(
            (e->name && *e->name) ? e->name : _("Unnamed"));
        gtk_widget_add_css_class(pill, "pill");
        g_object_set_data(G_OBJECT(pill), "snippet-idx", GUINT_TO_POINTER(i));
        g_signal_connect(pill, "clicked", G_CALLBACK(on_snippet_pill_clicked), self);
        gtk_flow_box_insert(self->snippet_flow_box, pill, -1);
        /* Tag the wrapping FlowBoxChild so the filter can locate the entry. */
        GtkFlowBoxChild *child =
            gtk_flow_box_get_child_at_index(self->snippet_flow_box, (int)i);
        if (child)
            g_object_set_data(G_OBJECT(child), "snippet-idx", GUINT_TO_POINTER(i));
    }
}

void silktex_prefs_set_snippets(SilktexPrefs *self, SilktexSnippets *snippets)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    g_set_object(&self->snippets, snippets);

    AdwPreferencesPage *snip_page =
        ADW_PREFERENCES_PAGE(g_object_get_data(G_OBJECT(self), "snip-page"));
    if (!snip_page) return;

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

    AdwPreferencesGroup *grp_list = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_list, _("Manage Snippets"));
    adw_preferences_group_set_description(
        grp_list, _("Stored as VS Code-style JSON in snippets.json.  SilkTex adds "
                    "an optional \"accelerator\" field for global shortcut letters."));

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_widget_set_margin_bottom(toolbar, 4);

    GtkWidget *btn_save   = gtk_button_new_with_label(_("Save"));
    GtkWidget *btn_reset  = gtk_button_new_with_label(_("Reset"));
    GtkWidget *btn_new    = gtk_button_new_with_label(_("New"));
    GtkWidget *btn_import = gtk_button_new_with_label(_("Import…"));
    gtk_widget_add_css_class(btn_save, "suggested-action");
    gtk_widget_set_hexpand(btn_save, FALSE);
    g_signal_connect(btn_save,   "clicked", G_CALLBACK(on_snippet_save),   self);
    g_signal_connect(btn_reset,  "clicked", G_CALLBACK(on_snippet_reset),  self);
    g_signal_connect(btn_new,    "clicked", G_CALLBACK(on_snippet_new),    self);
    g_signal_connect(btn_import, "clicked", G_CALLBACK(on_snippet_import), self);

    gtk_box_append(GTK_BOX(toolbar), btn_new);
    gtk_box_append(GTK_BOX(toolbar), btn_import);
    gtk_box_append(GTK_BOX(toolbar), btn_save);
    gtk_box_append(GTK_BOX(toolbar), btn_reset);

    GtkWidget *search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search),
                                          _("Search snippets by name or text…"));
    gtk_widget_set_margin_top(search, 4);
    g_signal_connect(search, "search-changed",
                     G_CALLBACK(on_snippet_search_changed), self);

    self->snippet_flow_box = GTK_FLOW_BOX(gtk_flow_box_new());
    gtk_flow_box_set_selection_mode(self->snippet_flow_box, GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(self->snippet_flow_box, 4);
    gtk_flow_box_set_min_children_per_line(self->snippet_flow_box, 1);
    gtk_flow_box_set_row_spacing(self->snippet_flow_box, 8);
    gtk_flow_box_set_column_spacing(self->snippet_flow_box, 8);
    gtk_widget_set_margin_top(GTK_WIDGET(self->snippet_flow_box), 8);
    gtk_widget_set_margin_bottom(GTK_WIDGET(self->snippet_flow_box), 8);
    gtk_flow_box_set_filter_func(self->snippet_flow_box,
                                 snippet_pill_filter, self, NULL);

    adw_preferences_group_add(grp_list, toolbar);
    adw_preferences_group_add(grp_list, search);
    adw_preferences_group_add(grp_list, GTK_WIDGET(self->snippet_flow_box));
    adw_preferences_page_add(snip_page, grp_list);

    snippets_parse_file(self);
    snippets_rebuild_pills(self);
}

void silktex_prefs_present(SilktexPrefs *self, GtkWindow *parent)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    adw_dialog_present(ADW_DIALOG(self), GTK_WIDGET(parent));
}
