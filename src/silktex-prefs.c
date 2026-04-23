/*
 * SilkTex - Preferences dialog
 * SPDX-License-Identifier: MIT
 *
 * All settings are written immediately to configfile and the apply-callback
 * is fired so the window can push them to every open editor.
 */
#include "silktex-prefs.h"
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
    AdwSpinRow  *row_tabwidth;
    AdwComboRow *row_scheme;

    /* Compile page */
    AdwComboRow *row_typesetter;
    AdwSwitchRow *row_shellescape;
    AdwSwitchRow *row_synctex;
    AdwSwitchRow *row_auto_compile;
    AdwSpinRow  *row_compile_timer;

    /* Preview page */
    AdwSwitchRow *row_autosave;
    AdwSpinRow  *row_autosave_timer;

    GtkStringList *scheme_ids;   /* parallel to the combo model */
};

G_DEFINE_FINAL_TYPE(SilktexPrefs, silktex_prefs, ADW_TYPE_PREFERENCES_DIALOG)

/* ------------------------------------------------------------------ helpers */

static void
fire_apply(SilktexPrefs *self)
{
    if (self->apply_func)
        self->apply_func(self->apply_data);
}

/* Build a GtkStringList whose display names match the style scheme list.
 * Also fill self->scheme_ids with the corresponding IDs. */
static GtkStringList *
build_scheme_model(SilktexPrefs *self)
{
    GtkSourceStyleSchemeManager *mgr = gtk_source_style_scheme_manager_get_default();
    const char * const *ids = gtk_source_style_scheme_manager_get_scheme_ids(mgr);

    /* add user scheme dir */
    g_autofree char *confdir = C_GUMMI_CONFDIR;
    g_autofree char *custom  = g_build_filename(confdir, "styles", NULL);
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

static int
scheme_index_for_id(SilktexPrefs *self, const char *id)
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

/* ------------------------------------------------------------------ signals */

static void on_line_numbers(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Editor", "line_numbers", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_highlighting(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Editor", "highlighting", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_textwrap(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    gboolean val = adw_switch_row_get_active(r);
    config_set_boolean("Editor", "textwrapping", val);
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_wordwrap), val);
    fire_apply(self);
}
static void on_wordwrap(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Editor", "wordwrapping", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_autoindent(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Editor", "autoindentation", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_spaces_tabs(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Editor", "spaces_instof_tabs", adw_switch_row_get_active(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_tabwidth(AdwSpinRow *r, GParamSpec *p, gpointer ud) {
    config_set_integer("Editor", "tabwidth", (int)adw_spin_row_get_value(r));
    fire_apply(SILKTEX_PREFS(ud));
}
static void on_scheme(AdwComboRow *r, GParamSpec *p, gpointer ud) {
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    guint idx = adw_combo_row_get_selected(r);
    if (idx == GTK_INVALID_LIST_POSITION) return;
    GtkStringObject *obj = g_list_model_get_item(G_LIST_MODEL(self->scheme_ids), idx);
    if (!obj) return;
    config_set_string("Editor", "style_scheme", gtk_string_object_get_string(obj));
    g_object_unref(obj);
    fire_apply(self);
}

static void on_shellescape(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Compile", "shellescape", adw_switch_row_get_active(r));
}
static void on_synctex(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Compile", "synctex", adw_switch_row_get_active(r));
}
static void on_auto_compile(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    config_set_boolean("Compile", "auto_compile", adw_switch_row_get_active(r));
}
static void on_compile_timer(AdwSpinRow *r, GParamSpec *p, gpointer ud) {
    config_set_integer("Compile", "timer", (int)adw_spin_row_get_value(r));
}
static void on_typesetter(AdwComboRow *r, GParamSpec *p, gpointer ud) {
    static const char *ts[] = { "pdflatex", "xelatex", "lualatex", "latexmk" };
    guint idx = adw_combo_row_get_selected(r);
    if (idx < G_N_ELEMENTS(ts))
        config_set_string("Compile", "typesetter", ts[idx]);
}

static void on_autosave(AdwSwitchRow *r, GParamSpec *p, gpointer ud) {
    SilktexPrefs *self = SILKTEX_PREFS(ud);
    gboolean val = adw_switch_row_get_active(r);
    config_set_boolean("File", "autosaving", val);
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_autosave_timer), val);
}
static void on_autosave_timer(AdwSpinRow *r, GParamSpec *p, gpointer ud) {
    config_set_integer("File", "autosave_timer", (int)adw_spin_row_get_value(r));
}

/* ------------------------------------------------------------------ init */

static void
silktex_prefs_init(SilktexPrefs *self)
{
    /* ---- Editor page ---- */
    AdwPreferencesPage *editor_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(editor_page, _("Editor"));
    adw_preferences_page_set_icon_name(editor_page, "document-edit-symbolic");

    AdwPreferencesGroup *grp_display = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(grp_display, _("Display"));

    self->row_line_numbers = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_line_numbers), _("Show Line Numbers"));
    adw_switch_row_set_active(self->row_line_numbers, config_get_boolean("Editor", "line_numbers"));
    g_signal_connect(self->row_line_numbers, "notify::active", G_CALLBACK(on_line_numbers), self);

    self->row_highlighting = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_highlighting), _("Highlight Current Line"));
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
    adw_switch_row_set_active(self->row_autoindent, config_get_boolean("Editor", "autoindentation"));
    g_signal_connect(self->row_autoindent, "notify::active", G_CALLBACK(on_autoindent), self);

    self->row_spaces_tabs = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_spaces_tabs), _("Spaces Instead of Tabs"));
    adw_switch_row_set_active(self->row_spaces_tabs, config_get_boolean("Editor", "spaces_instof_tabs"));
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

    GtkStringList *ts_list = gtk_string_list_new(
        (const char *[]){ "pdflatex", "xelatex", "lualatex", "latexmk", NULL });

    self->row_typesetter = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_typesetter), _("Typesetter"));
    adw_combo_row_set_model(self->row_typesetter, G_LIST_MODEL(ts_list));

    const char *cur_ts = config_get_string("Compile", "typesetter");
    static const char *ts_vals[] = { "pdflatex", "xelatex", "lualatex", "latexmk" };
    for (int i = 0; i < (int)G_N_ELEMENTS(ts_vals); i++) {
        if (g_strcmp0(cur_ts, ts_vals[i]) == 0) {
            adw_combo_row_set_selected(self->row_typesetter, (guint)i);
            break;
        }
    }
    g_signal_connect(self->row_typesetter, "notify::selected", G_CALLBACK(on_typesetter), self);

    self->row_shellescape = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_shellescape), _("Shell Escape (-shell-escape)"));
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
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_auto_compile), _("Auto-Compile on Edit"));
    adw_switch_row_set_active(self->row_auto_compile, config_get_boolean("Compile", "auto_compile"));
    g_signal_connect(self->row_auto_compile, "notify::active", G_CALLBACK(on_auto_compile), self);

    self->row_compile_timer = ADW_SPIN_ROW(adw_spin_row_new_with_range(1, 30, 1));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_compile_timer), _("Compile Delay (seconds)"));
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
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->row_autosave_timer), _("Auto-Save Interval (minutes)"));
    adw_spin_row_set_value(self->row_autosave_timer, config_get_integer("File", "autosave_timer"));
    gtk_widget_set_sensitive(GTK_WIDGET(self->row_autosave_timer),
                             config_get_boolean("File", "autosaving"));
    g_signal_connect(self->row_autosave_timer, "notify::value", G_CALLBACK(on_autosave_timer), self);

    adw_preferences_group_add(grp_autosave, GTK_WIDGET(self->row_autosave));
    adw_preferences_group_add(grp_autosave, GTK_WIDGET(self->row_autosave_timer));
    adw_preferences_page_add(files_page, grp_autosave);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(self), files_page);
}

static void
silktex_prefs_class_init(SilktexPrefsClass *klass)
{
}

SilktexPrefs *
silktex_prefs_new(void)
{
    return g_object_new(SILKTEX_TYPE_PREFS, NULL);
}

void
silktex_prefs_set_apply_callback(SilktexPrefs *self,
                                  SilktexPrefsApplyFunc func,
                                  gpointer user_data)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    self->apply_func = func;
    self->apply_data = user_data;
}

void
silktex_prefs_present(SilktexPrefs *self, GtkWindow *parent)
{
    g_return_if_fail(SILKTEX_IS_PREFS(self));
    adw_dialog_present(ADW_DIALOG(self), GTK_WIDGET(parent));
}
