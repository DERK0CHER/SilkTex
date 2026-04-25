/*
 * SilkTex - Snippet engine
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GObject-owned snippet table + expansion state machine. The main window
 * installs key handlers that call silktex_snippets_handle_key/_release.
 *
 * Expansion model (matches original Gummi behaviour):
 *   1. Insert raw snippet body (including "$1", "${2:default}", etc.) at cursor.
 *   2. Create left/right gravity marks around every placeholder.
 *   3. "initial_expand": replace placeholder syntax with default text / macros.
 *      Because marks track buffer edits we can do this in any order.
 *   4. Tab / Shift-Tab cycle through unique groups by number; $0 is the final
 *      landing position; Escape deactivates.
 *
 * Snippet file format:
 *   VS Code-style JSON object in ~/.config/silktex/snippets.json.
 *   Each entry supports "prefix", "body", "description", and SilkTex's
 *   optional "accelerator" field for global-modifier shortcuts.
 */

#include "snippets.h"
#include "configfile.h"
#include "constants.h"
#include "utils.h"
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ types */

typedef struct {
    guint keyval;
    GdkModifierType mods;
    char *keyword; /* points into the slist, do NOT free separately */
} SnippetAccel;

typedef struct {
    glong group;        /* $N → N; macro ($FILENAME etc.) → -1  */
    int start;          /* char offset in raw body                */
    int len;            /* char length of placeholder syntax      */
    char *deftext;      /* default text or macro name             */
    GtkTextMark *lmark; /* left-gravity mark (inserted-at = stays left)  */
    GtkTextMark *rmark; /* right-gravity mark                     */
} Holder;

typedef struct {
    char *expanded;   /* raw body, inserted as-is then cleaned up */
    int start_offset; /* char offset in buffer where snippet starts */
    GList *holders;   /* Holder*, position-sorted                  */
    GList *unique;    /* one representative per group, group-sorted */
    GList *current;   /* pointer into unique – focused group        */
    char *sel_text;   /* captured selection for $SELECTED_TEXT      */
} SnippetState;

struct _SilktexSnippets {
    GObject parent_instance;
    char *filename;
    slist *head;                 /* linked list of snippets (first=header, second=body) */
    GList *accels;               /* SnippetAccel* for keyboard shortcuts                */
    GdkModifierType global_mods; /* modifiers combined with single-letter accels    */
    SnippetState *active;
    GList *stack;
};

G_DEFINE_FINAL_TYPE (SilktexSnippets, silktex_snippets, G_TYPE_OBJECT)

/* ---------------------------------------------------------------- default content
 *
 * We ship a default snippets.json in $datadir/silktex/snippets/snippets.json.
 * On first run (or after reset), we copy that file verbatim to the user's
 * config directory.  During development the build system exports GUMMI_DATA
 * which points at the in-tree data/ folder.
 */

#ifndef GUMMI_DATA
#define GUMMI_DATA "/usr/share/silktex"
#endif

    static char *find_default_snippets_path(void)
    {
        /* 1) development: GUMMI_DATA/snippets/snippets.json              */
        char *p = g_build_filename(GUMMI_DATA, "snippets", "snippets.json", NULL);
        if (g_file_test(p, G_FILE_TEST_IS_REGULAR)) return p;
        g_free(p);

        /* 2) installed prefix next to this binary (datadir/silktex/…)    */
        const char *const *dirs = g_get_system_data_dirs();
        for (int i = 0; dirs && dirs[i]; i++) {
            char *q = g_build_filename(dirs[i], "silktex", "snippets", "snippets.json", NULL);
            if (g_file_test(q, G_FILE_TEST_IS_REGULAR)) return q;
            g_free(q);
        }
        return NULL;
    }

static gboolean copy_default_snippets(const char *dest)
{
    g_autofree char *src = find_default_snippets_path();
    if (!src) return FALSE;
    g_autofree char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(src, &contents, &len, NULL)) return FALSE;
    return g_file_set_contents(dest, contents, (gssize)len, NULL);
}

/* ------------------------------------------------------------------ file I/O */

static void free_accels(GList *accels)
{
    for (GList *c = accels; c; c = c->next) {
        g_free(((SnippetAccel *)c->data)->keyword);
        g_free(c->data);
    }
    g_list_free(accels);
}

static void free_slist(slist *head)
{
    slist *c = head;
    while (c) {
        slist *nx = c->next;
        g_free(c->first);
        g_free(c->second);
        g_free(c);
        c = nx;
    }
}

/*
 * Parse the ACCEL field of a snippet header.
 *
 * Two formats are accepted:
 *   1. Legacy full accelerator string: "<Shift><Alt>e" or "<Control>F3"
 *      (anything gtk_accelerator_parse understands).
 *   2. New short form: a single letter/keyword such as "e" — the global
 *      modifier pair (set via silktex_snippets_set_modifiers) is applied.
 *
 * Empty / missing → no shortcut.
 */
static void parse_and_store_accel(SilktexSnippets *self, const char *header)
{
    gchar **parts = g_strsplit(header, ",", 3);
    if (!parts[0] || !parts[1] || !*parts[1]) {
        g_strfreev(parts);
        return;
    }

    const char *accel = parts[1];
    guint kv = 0;
    GdkModifierType mods = 0;

    if (strchr(accel, '<') != NULL) {
        /* Legacy explicit-modifier form: "<Shift><Alt>e" */
        gtk_accelerator_parse(accel, &kv, &mods);
    } else {
        /* Short form: just the letter; combine with global modifiers. */
        kv = gdk_keyval_from_name(accel);
        mods = self->global_mods;
    }

    /*
     * Safety: never register an accelerator with no modifiers — otherwise
     * every bare keystroke matching `kv` would trigger the snippet.  If
     * the user has misconfigured both modifier slots, the short-form
     * snippet is silently skipped; legacy snippets with explicit
     * modifiers are still honored.
     */
    if (kv != 0 && kv != GDK_KEY_VoidSymbol && mods != 0) {
        SnippetAccel *a = g_new0(SnippetAccel, 1);
        a->keyval = kv;
        a->mods = mods & gtk_accelerator_get_default_mod_mask();
        a->keyword = g_strdup(parts[0]);
        self->accels = g_list_append(self->accels, a);
    }
    g_strfreev(parts);
}

/*
 * Walk all stored snippet headers and (re)build self->accels.
 * Called after the file reloads or the global modifiers change.
 */
static void rebuild_accels(SilktexSnippets *self)
{
    free_accels(self->accels);
    self->accels = NULL;
    for (slist *c = self->head; c; c = c->next) {
        if (c->first) parse_and_store_accel(self, c->first);
    }
}

static GdkModifierType modifier_from_name(const char *name)
{
    if (!name || !*name) return 0;
    if (g_ascii_strcasecmp(name, "Shift") == 0) return GDK_SHIFT_MASK;
    if (g_ascii_strcasecmp(name, "Control") == 0) return GDK_CONTROL_MASK;
    if (g_ascii_strcasecmp(name, "Ctrl") == 0) return GDK_CONTROL_MASK;
    if (g_ascii_strcasecmp(name, "Alt") == 0) return GDK_ALT_MASK;
    if (g_ascii_strcasecmp(name, "Meta") == 0) return GDK_META_MASK;
    if (g_ascii_strcasecmp(name, "Super") == 0) return GDK_SUPER_MASK;
    return 0;
}

static void append_snippet(SilktexSnippets *self, const char *prefix, const char *accelerator,
                           const char *name, const char *body)
{
    if (!prefix || !*prefix) return;

    slist *node = g_new0(slist, 1);
    node->first =
        g_strdup_printf("%s,%s,%s", prefix, accelerator ? accelerator : "", name ? name : prefix);
    node->second = g_strdup(body ? body : "");

    if (!self->head) {
        self->head = node;
        return;
    }

    slist *tail = self->head;
    while (tail->next) tail = tail->next;
    tail->next = node;
}

static char *snippet_body_from_json_member(JsonObject *obj)
{
    if (!json_object_has_member(obj, "body")) return g_strdup("");

    JsonNode *body_node = json_object_get_member(obj, "body");
    if (JSON_NODE_HOLDS_VALUE(body_node)) {
        const char *body = json_node_get_string(body_node);
        return g_strdup(body ? body : "");
    }

    if (JSON_NODE_HOLDS_ARRAY(body_node)) {
        JsonArray *arr = json_node_get_array(body_node);
        GString *body = g_string_new(NULL);
        guint len = json_array_get_length(arr);
        for (guint i = 0; i < len; i++) {
            if (i > 0) g_string_append_c(body, '\n');
            const char *line = json_array_get_string_element(arr, i);
            g_string_append(body, line ? line : "");
        }
        return g_string_free(body, FALSE);
    }

    return g_strdup("");
}

static char *snippet_prefix_from_json_member(JsonObject *obj)
{
    if (!json_object_has_member(obj, "prefix")) return g_strdup("");

    JsonNode *prefix_node = json_object_get_member(obj, "prefix");
    if (JSON_NODE_HOLDS_VALUE(prefix_node)) {
        const char *prefix = json_node_get_string(prefix_node);
        return g_strdup(prefix ? prefix : "");
    }

    if (JSON_NODE_HOLDS_ARRAY(prefix_node)) {
        JsonArray *arr = json_node_get_array(prefix_node);
        if (json_array_get_length(arr) > 0) {
            const char *prefix = json_array_get_string_element(arr, 0);
            return g_strdup(prefix ? prefix : "");
        }
    }

    return g_strdup("");
}

static gboolean load_json_snippets(SilktexSnippets *self, const char *path)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    GError *error = NULL;
    if (!json_parser_load_from_file(parser, path, &error)) {
        slog(L_WARNING, "Snippets: failed to parse %s: %s\n", path,
             error ? error->message : "unknown error");
        g_clear_error(&error);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) return FALSE;

    JsonObject *root_obj = json_node_get_object(root);
    g_autoptr(GList) members = json_object_get_members(root_obj);

    for (GList *l = members; l; l = l->next) {
        const char *name = l->data;
        JsonObject *entry = json_object_get_object_member(root_obj, name);
        if (!entry) continue;

        g_autofree char *prefix = snippet_prefix_from_json_member(entry);
        g_autofree char *body = snippet_body_from_json_member(entry);
        const char *description =
            json_object_has_member(entry, "description")
                ? json_object_get_string_member(entry, "description")
                : name;
        const char *accelerator =
            json_object_has_member(entry, "accelerator")
                ? json_object_get_string_member(entry, "accelerator")
                : "";

        append_snippet(self, prefix, accelerator, description && *description ? description : name,
                       body);
    }

    return TRUE;
}

static gboolean load_legacy_cfg_into_memory(SilktexSnippets *self, const char *path)
{
    FILE *fh = fopen(path, "r");
    if (!fh) return FALSE;

    char buf[BUFSIZ];
    slist *prev = NULL;

    while (fgets(buf, sizeof(buf), fh)) {
        int len = (int)strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        if (!buf[0] || buf[0] == '#') continue;

        if (buf[0] != '\t') {
            char *sp = strchr(buf, ' ');
            if (!sp) continue;
            const char *header = sp + 1;
            slist *node = g_new0(slist, 1);
            node->first = g_strdup(header);
            if (!self->head) self->head = node;
            if (prev) prev->next = node;
            prev = node;
        } else if (prev) {
            const char *line = buf + 1;
            if (!prev->second) {
                prev->second = g_strdup(line);
            } else {
                char *old = prev->second;
                prev->second = g_strconcat(old, "\n", line, NULL);
                g_free(old);
            }
        }
    }
    fclose(fh);
    return TRUE;
}

static void add_json_body(JsonBuilder *builder, const char *body)
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

static gboolean save_memory_as_json(SilktexSnippets *self, const char *path, GError **error)
{
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);

    for (slist *c = self->head; c; c = c->next) {
        gchar **parts = g_strsplit(c->first ? c->first : "", ",", 3);
        const char *prefix = parts[0] ? parts[0] : "";
        const char *accelerator = parts[1] ? parts[1] : "";
        const char *name = (parts[2] && *parts[2]) ? parts[2] : prefix;

        json_builder_set_member_name(builder, name);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "prefix");
        json_builder_add_string_value(builder, prefix);
        if (accelerator && *accelerator) {
            json_builder_set_member_name(builder, "accelerator");
            json_builder_add_string_value(builder, accelerator);
        }
        json_builder_set_member_name(builder, "body");
        add_json_body(builder, c->second);
        json_builder_set_member_name(builder, "description");
        json_builder_add_string_value(builder, name);
        json_builder_end_object(builder);

        g_strfreev(parts);
    }

    json_builder_end_object(builder);

    g_autoptr(JsonGenerator) gen = json_generator_new();
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, TRUE);
    json_generator_set_indent(gen, 2);
    return json_generator_to_file(gen, path, error);
}

static gboolean migrate_legacy_cfg(SilktexSnippets *self, const char *legacy_path)
{
    if (!g_file_test(legacy_path, G_FILE_TEST_IS_REGULAR)) return FALSE;
    if (!load_legacy_cfg_into_memory(self, legacy_path)) return FALSE;

    GError *error = NULL;
    gboolean ok = save_memory_as_json(self, self->filename, &error);
    if (!ok) {
        slog(L_WARNING, "Snippets: failed to migrate %s to JSON: %s\n", legacy_path,
             error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    return ok;
}

static void load_snippets_file(SilktexSnippets *self)
{
    free_slist(self->head);
    self->head = NULL;
    free_accels(self->accels);
    self->accels = NULL;

    if (!g_file_test(self->filename, G_FILE_TEST_IS_REGULAR)) {
        g_autofree char *confdir = g_path_get_dirname(self->filename);
        g_autofree char *legacy = g_build_filename(confdir, "snippets.cfg", NULL);
        if (migrate_legacy_cfg(self, legacy)) {
            free_slist(self->head);
            self->head = NULL;
        } else {
            slog(L_WARNING, "Snippets: seeding %s from default template\n", self->filename);
        }
    }

    if (!g_file_test(self->filename, G_FILE_TEST_IS_REGULAR)) {
        if (!copy_default_snippets(self->filename)) {
            slog(L_WARNING, "Snippets: no default template found; starting empty\n");
            g_file_set_contents(self->filename, "{\n}\n", -1, NULL);
        }
    }

    load_json_snippets(self, self->filename);

    rebuild_accels(self);
}

static const char *snippet_lookup(SilktexSnippets *self, const char *keyword)
{
    for (slist *c = self->head; c; c = c->next) {
        const char *comma = strchr(c->first, ',');
        int klen = comma ? (int)(comma - c->first) : (int)strlen(c->first);
        if ((int)strlen(keyword) == klen && strncmp(c->first, keyword, (size_t)klen) == 0)
            return c->second;
    }
    return NULL;
}

/* ------------------------------------------------------------------ parser */

static gint cmp_by_start(gconstpointer a, gconstpointer b)
{
    return ((Holder *)a)->start - ((Holder *)b)->start;
}
static gint cmp_by_group(gconstpointer a, gconstpointer b)
{
    glong ga = ((Holder *)a)->group, gb = ((Holder *)b)->group;
    return (ga < gb) ? -1 : (ga > gb) ? 1 : 0;
}

/* byte offset in UTF-8 string → character offset */
static int boff2coff(const char *s, int boff)
{
    return (int)g_utf8_strlen(s, boff);
}

static SnippetState *parse_snippet(const char *body, const char *sel_text)
{
    SnippetState *s = g_new0(SnippetState, 1);
    s->expanded = g_strdup(body); /* inserted verbatim, cleaned up later */
    s->sel_text = g_strdup(sel_text ? sel_text : "");

    static const struct {
        const char *pat;
        gboolean is_num;
    } pats[] = {
        {"\\$\\{([0-9]+):?([^}]*)\\}", TRUE},
        {"\\$([0-9]+)", TRUE},
        {"\\$(SELECTED_TEXT|FILENAME|BASENAME)", FALSE},
        {"\\$\\{(SELECTED_TEXT|FILENAME|BASENAME)\\}", FALSE},
    };

    GList *holders = NULL;

    for (int pi = 0; pi < (int)G_N_ELEMENTS(pats); pi++) {
        GError *err = NULL;
        GRegex *re = g_regex_new(pats[pi].pat, G_REGEX_DOTALL, 0, &err);
        if (!re) {
            g_clear_error(&err);
            continue;
        }

        GMatchInfo *mi = NULL;
        g_regex_match(re, body, 0, &mi);
        while (g_match_info_matches(mi)) {
            int sb, eb;
            g_match_info_fetch_pos(mi, 0, &sb, &eb);

            Holder *h = g_new0(Holder, 1);
            h->start = boff2coff(body, sb);
            h->len = boff2coff(body, eb) - h->start;

            if (pats[pi].is_num) {
                g_autofree char *gn = g_match_info_fetch(mi, 1);
                g_autofree char *dt = g_match_info_fetch(mi, 2);
                h->group = atol(gn ? gn : "0");
                h->deftext = g_strdup(dt ? dt : "");
            } else {
                g_autofree char *kw = g_match_info_fetch(mi, 1);
                h->group = -1;
                h->deftext = g_strdup(kw ? kw : "");
            }
            holders = g_list_prepend(holders, h);
            g_match_info_next(mi, NULL);
        }
        g_match_info_free(mi);
        g_regex_unref(re);
    }

    s->holders = g_list_sort(holders, cmp_by_start);

    /* Build unique-group list */
    GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (GList *c = s->holders; c; c = c->next) {
        Holder *h = c->data;
        if (!g_hash_table_contains(seen, GINT_TO_POINTER((int)h->group))) {
            g_hash_table_add(seen, GINT_TO_POINTER((int)h->group));
            s->unique = g_list_prepend(s->unique, h);
        }
    }
    g_hash_table_destroy(seen);
    s->unique = g_list_sort(s->unique, cmp_by_group);

    return s;
}

/* ------------------------------------------------------------------ marks */

static void create_marks(SnippetState *s, GtkTextBuffer *buf)
{
    for (GList *c = s->holders; c; c = c->next) {
        Holder *h = c->data;
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_offset(buf, &it, s->start_offset + h->start);
        h->lmark = gtk_text_buffer_create_mark(buf, NULL, &it, TRUE);
        gtk_text_buffer_get_iter_at_offset(buf, &it, s->start_offset + h->start + h->len);
        h->rmark = gtk_text_buffer_create_mark(buf, NULL, &it, FALSE);
    }
}

static void delete_marks(SnippetState *s, GtkTextBuffer *buf)
{
    for (GList *c = s->holders; c; c = c->next) {
        Holder *h = c->data;
        if (h->lmark && !gtk_text_mark_get_deleted(h->lmark))
            gtk_text_buffer_delete_mark(buf, h->lmark);
        if (h->rmark && !gtk_text_mark_get_deleted(h->rmark))
            gtk_text_buffer_delete_mark(buf, h->rmark);
    }
}

static void free_state(SnippetState *s, GtkTextBuffer *buf)
{
    if (!s) return;
    delete_marks(s, buf);
    for (GList *c = s->holders; c; c = c->next) {
        g_free(((Holder *)c->data)->deftext);
        g_free(c->data);
    }
    g_list_free(s->holders);
    g_list_free(s->unique);
    g_free(s->expanded);
    g_free(s->sel_text);
    g_free(s);
}

/* ------------------------------------------------------------------ expand */

/*
 * Replace each placeholder's text in the buffer with the default / macro value.
 * We process in REVERSE position order so that earlier char offsets remain
 * valid after each deletion+insertion (though marks handle this anyway, this
 * order avoids mark-overlap edge cases).
 */
static void initial_expand(SnippetState *s, GtkTextBuffer *buf, const char *filename,
                           const char *basename)
{
    /* Build group→representative_deftext map */
    GHashTable *group_text = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (GList *c = s->unique; c; c = c->next) {
        Holder *h = c->data;
        g_hash_table_insert(group_text, GINT_TO_POINTER((int)h->group), h->deftext);
    }

    /* Reverse list of holders so we process last-to-first */
    GList *rev = g_list_reverse(g_list_copy(s->holders));

    for (GList *c = rev; c; c = c->next) {
        Holder *h = c->data;
        GtkTextIter ls, le;
        gtk_text_buffer_get_iter_at_mark(buf, &ls, h->lmark);
        gtk_text_buffer_get_iter_at_mark(buf, &le, h->rmark);

        /* Delete the raw placeholder syntax (e.g. "${1:default}" or "$1") */
        gtk_text_buffer_delete(buf, &ls, &le);

        /* Determine replacement */
        const char *repl = NULL;
        if (h->group == -1) {
            if (g_strcmp0(h->deftext, "SELECTED_TEXT") == 0)
                repl = s->sel_text;
            else if (g_strcmp0(h->deftext, "FILENAME") == 0)
                repl = filename;
            else if (g_strcmp0(h->deftext, "BASENAME") == 0)
                repl = basename;
        } else {
            repl = g_hash_table_lookup(group_text, GINT_TO_POINTER((int)h->group));
        }
        if (repl && *repl) gtk_text_buffer_insert(buf, &ls, repl, -1);
    }

    g_list_free(rev);
    g_hash_table_destroy(group_text);
}

/* ------------------------------------------------------------------ focus/nav */

static void focus_holder(Holder *h, GtkTextBuffer *buf, GtkTextView *view)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_mark(buf, &start, h->lmark);
    gtk_text_buffer_get_iter_at_mark(buf, &end, h->rmark);
    gtk_text_buffer_select_range(buf, &start, &end);
    gtk_text_view_scroll_to_mark(view, gtk_text_buffer_get_insert(buf), 0.1, FALSE, 0.0, 0.5);
}

static gboolean goto_next(SnippetState *s, GtkTextBuffer *buf, GtkTextView *view)
{
    if (!s->current)
        s->current = s->unique;
    else
        s->current = g_list_next(s->current);

    while (s->current) {
        glong g = ((Holder *)s->current->data)->group;
        if (g > 0) break;
        s->current = g_list_next(s->current);
    }

    if (!s->current) {
        /* Gummi behavior: final landing on $0 if present */
        for (GList *c = s->unique; c; c = c->next) {
            if (((Holder *)c->data)->group == 0) {
                focus_holder(c->data, buf, view);
                return FALSE;
            }
        }
        return FALSE;
    }

    focus_holder(s->current->data, buf, view);
    return TRUE;
}

static gboolean goto_prev(SnippetState *s, GtkTextBuffer *buf, GtkTextView *view)
{
    GList *prev = s->current ? g_list_previous(s->current) : NULL;
    while (prev) {
        if (((Holder *)prev->data)->group >= 0) break;
        prev = g_list_previous(prev);
    }
    if (!prev) return FALSE;
    s->current = prev;
    focus_holder(s->current->data, buf, view);
    return TRUE;
}

/* ------------------------------------------------------------------ mirror */

static void sync_group(SnippetState *s, GtkTextBuffer *buf)
{
    if (!s->current) return;
    Holder *active = s->current->data;
    if (active->group <= 0) return;

    GtkTextIter ls, le;
    gtk_text_buffer_get_iter_at_mark(buf, &ls, active->lmark);
    gtk_text_buffer_get_iter_at_mark(buf, &le, active->rmark);
    g_autofree char *text = gtk_text_iter_get_text(&ls, &le);

    for (GList *c = s->holders; c; c = c->next) {
        Holder *h = c->data;
        if (h == active || h->group != active->group) continue;
        GtkTextIter ms, me;
        gtk_text_buffer_get_iter_at_mark(buf, &ms, h->lmark);
        gtk_text_buffer_get_iter_at_mark(buf, &me, h->rmark);
        gtk_text_buffer_delete(buf, &ms, &me);
        gtk_text_buffer_insert(buf, &ms, text, -1);
    }
}

/* ------------------------------------------------------------------ activate */

static void deactivate(SilktexSnippets *self, GtkTextBuffer *buf)
{
    if (!self->active) return;
    free_state(self->active, buf);
    GList *last = g_list_last(self->stack);
    if (last) {
        self->active = last->data;
        self->stack = g_list_delete_link(self->stack, last);
    } else {
        self->active = NULL;
    }
}

/*
 * activate_snippet:
 *   @delete_keyword: TRUE when expanding via Tab (delete typed word first),
 *                    FALSE when expanding via accelerator (insert at cursor).
 */
static void activate_snippet(SilktexSnippets *self, SilktexEditor *editor, const char *keyword,
                             gboolean delete_keyword)
{
    const char *body = snippet_lookup(self, keyword);
    if (!body) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(editor);
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(sbuf);
    GtkTextView *view = GTK_TEXT_VIEW(silktex_editor_get_view(editor));

    /* Capture selection for $SELECTED_TEXT */
    GtkTextIter sel_s, sel_e;
    gtk_text_buffer_get_selection_bounds(buf, &sel_s, &sel_e);
    g_autofree char *sel = gtk_text_iter_get_text(&sel_s, &sel_e);
    const char *filename = silktex_editor_get_filename(editor);
    g_autofree char *basename = filename ? g_path_get_basename(filename) : g_strdup("");

    SnippetState *s = parse_snippet(body, sel);

    gtk_text_buffer_begin_user_action(buf);

    if (delete_keyword) {
        /* Tab expansion: delete the typed keyword */
        GtkTextIter cur;
        gtk_text_buffer_get_iter_at_mark(buf, &cur, gtk_text_buffer_get_insert(buf));
        GtkTextIter word_start = cur;
        gtk_text_iter_backward_word_start(&word_start);
        gtk_text_buffer_delete(buf, &word_start, &cur);
    } else {
        /* Accelerator: delete any selection (for $SELECTED_TEXT wrapping) */
        if (*sel) gtk_text_buffer_delete(buf, &sel_s, &sel_e);
    }

    /* Record insertion point */
    GtkTextIter anchor_it;
    gtk_text_buffer_get_iter_at_mark(buf, &anchor_it, gtk_text_buffer_get_insert(buf));
    GtkTextMark *anchor = gtk_text_buffer_create_mark(buf, NULL, &anchor_it, TRUE);

    /* Insert raw body (with $1, ${2:default} etc.) */
    gtk_text_buffer_insert_at_cursor(buf, s->expanded, -1);

    gtk_text_buffer_get_iter_at_mark(buf, &anchor_it, anchor);
    s->start_offset = gtk_text_iter_get_offset(&anchor_it);
    gtk_text_buffer_delete_mark(buf, anchor);

    /* Create marks, then replace placeholder syntax with defaults */
    create_marks(s, buf);
    initial_expand(s, buf, filename ? filename : "", basename);

    gtk_text_buffer_end_user_action(buf);

    /* Push existing active snippet; make this one active */
    if (self->active) {
        sync_group(self->active, buf);
        self->stack = g_list_append(self->stack, self->active);
    }
    self->active = s;

    if (!goto_next(s, buf, view)) deactivate(self, buf);
}

/* ------------------------------------------------------------------ GObject */

static void silktex_snippets_finalize(GObject *obj)
{
    SilktexSnippets *self = SILKTEX_SNIPPETS(obj);
    g_free(self->filename);
    free_slist(self->head);
    free_accels(self->accels);
    G_OBJECT_CLASS(silktex_snippets_parent_class)->finalize(obj);
}
static void silktex_snippets_class_init(SilktexSnippetsClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = silktex_snippets_finalize;
}
static void silktex_snippets_init(SilktexSnippets *self) {}

/* ------------------------------------------------------------------ public */

SilktexSnippets *silktex_snippets_new(void)
{
    SilktexSnippets *self = g_object_new(SILKTEX_TYPE_SNIPPETS, NULL);
    char *confdir = C_GUMMI_CONFDIR;
    g_mkdir_with_parents(confdir, DIR_PERMS);
    self->filename = g_build_filename(confdir, "snippets.json", NULL);
    g_free(confdir);

    /* Default global modifier pair (user-overridable via prefs).
     * Matches the original Gummi default (<Shift><Alt>). */
    self->global_mods = GDK_SHIFT_MASK | GDK_ALT_MASK;

    load_snippets_file(self);
    return self;
}

const char *silktex_snippets_get_filename(SilktexSnippets *self)
{
    g_return_val_if_fail(SILKTEX_IS_SNIPPETS(self), NULL);
    return self->filename;
}

void silktex_snippets_reload(SilktexSnippets *self)
{
    g_return_if_fail(SILKTEX_IS_SNIPPETS(self));
    load_snippets_file(self);
}

void silktex_snippets_reset_to_default(SilktexSnippets *self)
{
    g_return_if_fail(SILKTEX_IS_SNIPPETS(self));
    /* Remove the user's copy; load_snippets_file() will re-seed it. */
    g_unlink(self->filename);
    load_snippets_file(self);
}

void silktex_snippets_set_modifiers(SilktexSnippets *self, const char *modifier1,
                                    const char *modifier2)
{
    g_return_if_fail(SILKTEX_IS_SNIPPETS(self));
    GdkModifierType m = modifier_from_name(modifier1) | modifier_from_name(modifier2);
    self->global_mods = m;
    rebuild_accels(self);
}

gboolean silktex_snippets_handle_key(SilktexSnippets *self, SilktexEditor *editor, guint keyval,
                                     GdkModifierType state)
{
    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(editor);
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(sbuf);
    GtkTextView *view = GTK_TEXT_VIEW(silktex_editor_get_view(editor));

    /* Escape: deactivate */
    if (keyval == GDK_KEY_Escape && self->active) {
        deactivate(self, buf);
        return TRUE;
    }

    /* Check accelerator-based snippet triggers first */
    GdkModifierType clean = state & gtk_accelerator_get_default_mod_mask();
    guint lower_kv = gdk_keyval_to_lower(keyval);
    for (GList *c = self->accels; c; c = c->next) {
        SnippetAccel *a = c->data;
        if (gdk_keyval_to_lower(a->keyval) == lower_kv && a->mods == clean) {
            activate_snippet(self, editor, a->keyword, FALSE);
            return TRUE;
        }
    }

    /* Gummi behavior: on Tab, try keyword expansion before placeholder nav */
    if (keyval == GDK_KEY_Tab && !(state & GDK_SHIFT_MASK)) {
        GtkTextIter cur;
        gtk_text_buffer_get_iter_at_mark(buf, &cur, gtk_text_buffer_get_insert(buf));
        if (gtk_text_iter_ends_word(&cur)) {
            GtkTextIter ws = cur;
            gtk_text_iter_backward_word_start(&ws);
            g_autofree char *word = gtk_text_iter_get_text(&ws, &cur);
            if (word && snippet_lookup(self, word)) {
                activate_snippet(self, editor, word, TRUE);
                return TRUE;
            }
        }
        if (self->active) {
            if (!goto_next(self->active, buf, view)) deactivate(self, buf);
            return TRUE;
        }
    }

    /* Shift-Tab: previous placeholder */
    if ((keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_Tab) && (state & GDK_SHIFT_MASK)) {
        if (self->active) {
            if (!goto_prev(self->active, buf, view)) deactivate(self, buf);
            return TRUE;
        }
    }

    /* Auto-deactivate if the cursor has wandered outside the snippet body.
     * Use the last holder mark as the end boundary and
     * start_offset as the start boundary, matching Gummi's behaviour. */
    if (self->active && self->active->holders) {
        GList *last_node = g_list_last(self->active->holders);
        if (last_node) {
            Holder *last_h = last_node->data;
            GtkTextIter cur_it, bound_it;
            gtk_text_buffer_get_iter_at_mark(buf, &cur_it, gtk_text_buffer_get_insert(buf));
            gtk_text_buffer_get_iter_at_mark(buf, &bound_it, last_h->lmark);
            int cur_off = gtk_text_iter_get_offset(&cur_it);
            int bound_end = gtk_text_iter_get_offset(&bound_it);
            if (cur_off < self->active->start_offset || cur_off > bound_end) deactivate(self, buf);
        }
    }

    return FALSE;
}

/*
 * silktex_snippets_handle_key_release:
 *
 * Must be called from the window's key-released signal so that group
 * mirroring fires AFTER the typed character has been committed to the
 * buffer (GTK processes input between key-press and key-release).
 * This matches Gummi's snippets_key_release_cb() behaviour exactly.
 */
gboolean silktex_snippets_handle_key_release(SilktexSnippets *self, SilktexEditor *editor,
                                             guint keyval, GdkModifierType state)
{
    (void)state;
    if (keyval == GDK_KEY_Tab) return FALSE;
    if (!self->active) return FALSE;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(editor);
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(sbuf);
    sync_group(self->active, buf);
    return FALSE;
}
