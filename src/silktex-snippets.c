/*
 * SilkTex - Snippet engine
 * SPDX-License-Identifier: MIT
 */
#include "silktex-snippets.h"
#include "configfile.h"
#include "constants.h"
#include "utils.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ types */

typedef struct {
    glong   group;     /* $N group number; -1 = special (FILENAME etc.) */
    gint    start;     /* UTF-8 char offset in expanded text             */
    gint    len;       /* length in chars                                */
    char   *text;      /* default text / special keyword                 */
    GtkTextMark *lmark; /* left gravity mark after expansion             */
    GtkTextMark *rmark; /* right gravity mark                            */
} SnippetHolder;

typedef struct {
    char     *snippet;      /* raw text with placeholders                 */
    char     *expanded;     /* placeholder-stripped expansion text        */
    gint      start_offset; /* buffer char-offset where snippet starts    */
    GList    *holders;      /* SnippetHolder*, sorted by start            */
    GList    *unique;       /* one entry per distinct group, sorted by #  */
    GList    *current;      /* pointer into unique – the focused group    */
    char     *sel_text;     /* text that was selected when snippet fired  */
} SnippetState;

struct _SilktexSnippets {
    GObject parent_instance;

    char    *filename;
    slist   *head;           /* linked list from utils.h: first=key second=body */

    SnippetState *active;    /* NULL when no snippet is being tabbed through */
    GList        *stack;     /* nested snippets (rare, but supported)         */
};

G_DEFINE_FINAL_TYPE(SilktexSnippets, silktex_snippets, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ helpers */

static const char *DEFAULT_SNIPPETS_DATA =
"snippet sec,<Ctrl>1,Section\n"
"\t\\section{$1}\n"
"snippet sub,<Ctrl>2,Subsection\n"
"\t\\subsection{$1}\n"
"snippet bf,,Bold\n"
"\t\\textbf{$1}\n"
"snippet em,,Emphasis\n"
"\t\\emph{$1}\n"
"snippet env,,Environment\n"
"\t\\begin{$1}\n"
"\t\t$2\n"
"\t\\end{$1}\n"
"snippet fig,,Figure\n"
"\t\\begin{figure}[htbp]\n"
"\t\t\\centering\n"
"\t\t\\includegraphics[width=0.8\\linewidth]{$1}\n"
"\t\t\\caption{$2}\n"
"\t\t\\label{fig:$3}\n"
"\t\\end{figure}\n";

static void snippet_state_free(SnippetState *s, GtkTextBuffer *buf);

static gint cmp_holder_start(gconstpointer a, gconstpointer b) {
    return ((SnippetHolder *)a)->start - ((SnippetHolder *)b)->start;
}
static gint cmp_holder_group(gconstpointer a, gconstpointer b) {
    glong ga = ((SnippetHolder *)a)->group;
    glong gb = ((SnippetHolder *)b)->group;
    return (ga < gb) ? -1 : (ga > gb) ? 1 : 0;
}

/* Parse "snippet KEY[,ACCEL[,Name]]" lines from the file */
static void
load_snippets_file(SilktexSnippets *self)
{
    FILE *fh = fopen(self->filename, "r");
    if (!fh) {
        slog(L_WARNING, "Snippets file not found, using defaults\n");
        /* write defaults */
        fh = fopen(self->filename, "w");
        if (fh) { fputs(DEFAULT_SNIPPETS_DATA, fh); fclose(fh); }
        fh = fopen(self->filename, "r");
        if (!fh) return;
    }

    char buf[BUFSIZ];
    slist *prev = NULL;

    while (fgets(buf, sizeof(buf), fh)) {
        int len = (int)strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

        if (buf[0] == '#' || buf[0] == '\0') continue;   /* comment / blank */

        if (buf[0] != '\t') {
            /* new snippet header: "snippet KEY,ACCEL,Name" */
            char *sp = strstr(buf, " ");
            if (!sp) continue;
            slist *node = g_new0(slist, 1);
            node->first = g_strdup(sp + 1);   /* "KEY,ACCEL,Name" */
            if (!self->head) self->head = node;
            if (prev) prev->next = node;
            prev = node;
        } else {
            /* body line (tab-indented) */
            if (!prev) continue;
            char *line = buf + 1;
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
}

static const char *
snippets_lookup(SilktexSnippets *self, const char *keyword)
{
    slist *cur = self->head;
    while (cur) {
        /* header: "KEY,ACCEL,Name" – compare prefix up to first comma */
        char *comma = strchr(cur->first, ',');
        int klen = comma ? (int)(comma - cur->first) : (int)strlen(cur->first);
        if ((int)strlen(keyword) == klen &&
            strncmp(cur->first, keyword, (size_t)klen) == 0) {
            return cur->second;
        }
        cur = cur->next;
    }
    return NULL;
}

/* ---- Expansion parser ---- */

static SnippetState *
parse_snippet(const char *snippet, const char *sel_text)
{
    SnippetState *s = g_new0(SnippetState, 1);
    s->snippet  = g_strdup(snippet);
    s->sel_text = g_strdup(sel_text ? sel_text : "");

    /* Patterns we recognise */
    static const struct { const char *pat; int is_num; } patterns[] = {
        { "\\$\\{([0-9]+):?([^}]*)\\}", 1 },
        { "\\$([0-9]+)",                1 },
        { "\\$(SELECTED_TEXT|FILENAME|BASENAME)", 0 },
        { "\\$\\{(SELECTED_TEXT|FILENAME|BASENAME)\\}", 0 },
    };

    GString *expanded = g_string_new(snippet);
    GList   *holders  = NULL;

    for (int pi = 0; pi < (int)G_N_ELEMENTS(patterns); pi++) {
        GError   *err = NULL;
        GRegex   *re  = g_regex_new(patterns[pi].pat, G_REGEX_DOTALL, 0, &err);
        if (!re) { g_error_free(err); continue; }

        GMatchInfo *mi = NULL;
        g_regex_match(re, snippet, 0, &mi);

        while (g_match_info_matches(mi)) {
            int start_b, end_b;
            g_match_info_fetch_pos(mi, 0, &start_b, &end_b);

            g_autofree char *prefix = g_substr((char *)snippet, 0, start_b);
            int start_ch = (int)g_utf8_strlen(prefix, -1);
            g_autofree char *full   = g_substr((char *)snippet, 0, end_b);
            int end_ch   = (int)g_utf8_strlen(full, -1);

            SnippetHolder *h = g_new0(SnippetHolder, 1);
            h->start = start_ch;
            h->len   = end_ch - start_ch;

            if (patterns[pi].is_num) {
                g_autofree char *gn = g_match_info_fetch(mi, 1);
                g_autofree char *dt = g_match_info_fetch(mi, 2);
                h->group = atol(gn);
                h->text  = g_strdup(dt ? dt : "");
            } else {
                g_autofree char *kw = g_match_info_fetch(mi, 1);
                h->group = -1;
                h->text  = g_strdup(kw);
            }
            holders = g_list_prepend(holders, h);
            g_match_info_next(mi, NULL);
        }
        g_match_info_free(mi);
        g_regex_unref(re);
    }

    /* Strip placeholders from expansion text */
    GRegex *strip = g_regex_new(
        "\\$(?:\\{[^}]*\\}|[0-9]+|[A-Z_]+)", G_REGEX_DOTALL, 0, NULL);
    g_autofree char *clean = g_regex_replace(strip, snippet, -1, 0, "", 0, NULL);
    g_regex_unref(strip);
    g_string_assign(expanded, clean);

    s->expanded = g_string_free(expanded, FALSE);
    s->holders  = g_list_sort(holders, cmp_holder_start);

    /* Build unique-group list */
    GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    GList *cur = s->holders;
    while (cur) {
        SnippetHolder *h = cur->data;
        if (!g_hash_table_contains(seen, GINT_TO_POINTER((int)h->group))) {
            g_hash_table_add(seen, GINT_TO_POINTER((int)h->group));
            s->unique = g_list_prepend(s->unique, h);
        }
        cur = cur->next;
    }
    g_hash_table_destroy(seen);
    s->unique = g_list_sort(s->unique, cmp_holder_group);

    return s;
}

static void
create_marks(SnippetState *s, GtkTextBuffer *buf, int base_offset)
{
    GList *cur = s->holders;
    while (cur) {
        SnippetHolder *h = cur->data;
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_offset(buf, &it, base_offset + h->start);
        h->lmark = gtk_text_buffer_create_mark(buf, NULL, &it, TRUE);
        gtk_text_buffer_get_iter_at_offset(buf, &it, base_offset + h->start + h->len);
        h->rmark = gtk_text_buffer_create_mark(buf, NULL, &it, FALSE);
        cur = cur->next;
    }
}

static void
delete_marks(SnippetState *s, GtkTextBuffer *buf)
{
    GList *cur = s->holders;
    while (cur) {
        SnippetHolder *h = cur->data;
        if (h->lmark && !gtk_text_mark_get_deleted(h->lmark))
            gtk_text_buffer_delete_mark(buf, h->lmark);
        if (h->rmark && !gtk_text_mark_get_deleted(h->rmark))
            gtk_text_buffer_delete_mark(buf, h->rmark);
        cur = cur->next;
    }
}

static void
snippet_state_free(SnippetState *s, GtkTextBuffer *buf)
{
    if (!s) return;
    delete_marks(s, buf);
    GList *cur = s->holders;
    while (cur) {
        SnippetHolder *h = cur->data;
        g_free(h->text);
        g_free(h);
        cur = cur->next;
    }
    g_list_free(s->holders);
    g_list_free(s->unique);
    g_free(s->snippet);
    g_free(s->expanded);
    g_free(s->sel_text);
    g_free(s);
}

/* Select the text covered by the current placeholder group */
static void
focus_current(SnippetState *s, GtkTextBuffer *buf)
{
    if (!s->current) return;
    SnippetHolder *h = s->current->data;
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_mark(buf, &start, h->lmark);
    gtk_text_buffer_get_iter_at_mark(buf, &end,   h->rmark);
    gtk_text_buffer_select_range(buf, &start, &end);
    gtk_text_view_scroll_to_mark(
        GTK_TEXT_VIEW(gtk_text_buffer_get_tag_table(buf)), /* harmless cast */
        gtk_text_buffer_get_insert(buf), 0.25, FALSE, 0, 0);
}

/* Advance to the next placeholder; returns FALSE when the snippet is done */
static gboolean
goto_next(SnippetState *s, GtkTextBuffer *buf)
{
    if (!s->current)
        s->current = s->unique;
    else
        s->current = g_list_next(s->current);

    /* skip $0 and negative (special) on first traversal */
    while (s->current) {
        SnippetHolder *h = s->current->data;
        if (h->group > 0) break;
        s->current = g_list_next(s->current);
    }

    if (!s->current) {
        /* look for $0 to land the cursor */
        GList *cur = s->unique;
        while (cur) {
            if (((SnippetHolder *)cur->data)->group == 0) {
                s->current = cur;
                focus_current(s, buf);
                return FALSE; /* deactivate after $0 */
            }
            cur = g_list_next(cur);
        }
        return FALSE;
    }

    focus_current(s, buf);
    return TRUE;
}

static gboolean
goto_prev(SnippetState *s, GtkTextBuffer *buf)
{
    if (!s->current || !g_list_previous(s->current))
        return FALSE;
    s->current = g_list_previous(s->current);
    while (s->current) {
        SnippetHolder *h = s->current->data;
        if (h->group > 0) break;
        s->current = g_list_previous(s->current);
    }
    if (!s->current) return FALSE;
    focus_current(s, buf);
    return TRUE;
}

/* ------------------------------------------------------------------ expand */

static void
activate_snippet(SilktexSnippets *self,
                 SilktexEditor   *editor,
                 const char      *key)
{
    const char *body = snippets_lookup(self, key);
    if (!body) return;

    GtkSourceBuffer *sbuf = silktex_editor_get_buffer(editor);
    GtkTextBuffer   *buf  = GTK_TEXT_BUFFER(sbuf);

    GtkTextIter sel_start, sel_end;
    gtk_text_buffer_get_selection_bounds(buf, &sel_start, &sel_end);
    g_autofree char *sel = gtk_text_iter_get_text(&sel_start, &sel_end);

    SnippetState *s = parse_snippet(body, sel);

    /* remove the keyword that was typed */
    GtkTextIter word_start = sel_start;
    gtk_text_iter_backward_word_start(&word_start);
    gtk_text_buffer_delete(buf, &word_start, &sel_start);

    /* record where the snippet begins */
    GtkTextMark *anchor = gtk_text_buffer_create_mark(buf, NULL, &word_start, TRUE);

    /* insert the expanded text */
    gtk_text_buffer_insert(buf, &word_start, s->expanded, -1);

    GtkTextIter anchor_it;
    gtk_text_buffer_get_iter_at_mark(buf, &anchor_it, anchor);
    s->start_offset = gtk_text_iter_get_offset(&anchor_it);
    gtk_text_buffer_delete_mark(buf, anchor);

    create_marks(s, buf, s->start_offset);

    /* push any existing active snippet onto the stack */
    if (self->active) {
        self->stack = g_list_append(self->stack, self->active);
    }
    self->active = s;

    if (!goto_next(s, buf)) {
        GtkTextBuffer *tbuf = GTK_TEXT_BUFFER(silktex_editor_get_buffer(editor));
        snippet_state_free(self->active, tbuf);
        self->active = NULL;
    }
}

static void
deactivate(SilktexSnippets *self, SilktexEditor *editor)
{
    if (!self->active) return;
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(silktex_editor_get_buffer(editor));
    snippet_state_free(self->active, buf);
    GList *last = g_list_last(self->stack);
    if (last) {
        self->active = last->data;
        self->stack  = g_list_delete_link(self->stack, last);
    } else {
        self->active = NULL;
    }
}

/* ------------------------------------------------------------------ GObject */

static void
silktex_snippets_finalize(GObject *obj)
{
    SilktexSnippets *self = SILKTEX_SNIPPETS(obj);
    g_free(self->filename);
    /* clean up slist */
    slist *cur = self->head;
    while (cur) { slist *nx = cur->next; g_free(cur->first); g_free(cur->second); g_free(cur); cur = nx; }
    G_OBJECT_CLASS(silktex_snippets_parent_class)->finalize(obj);
}

static void silktex_snippets_class_init(SilktexSnippetsClass *klass) {
    G_OBJECT_CLASS(klass)->finalize = silktex_snippets_finalize;
}
static void silktex_snippets_init(SilktexSnippets *self) {}

/* ------------------------------------------------------------------ public */

SilktexSnippets *
silktex_snippets_new(void)
{
    SilktexSnippets *self = g_object_new(SILKTEX_TYPE_SNIPPETS, NULL);

    char *confdir = C_GUMMI_CONFDIR;
    g_mkdir_with_parents(confdir, DIR_PERMS);

    self->filename = g_build_filename(confdir, "snippets.cfg", NULL);
    g_free(confdir);
    load_snippets_file(self);
    return self;
}

void silktex_snippets_load(SilktexSnippets *self)  { load_snippets_file(self); }
void silktex_snippets_save(SilktexSnippets *self)  { /* TODO: write back */ }
void silktex_snippets_reset_to_default(SilktexSnippets *self)
{
    if (g_file_set_contents(self->filename, DEFAULT_SNIPPETS_DATA, -1, NULL))
        load_snippets_file(self);
}

gboolean
silktex_snippets_handle_key(SilktexSnippets *self,
                             SilktexEditor   *editor,
                             guint            keyval,
                             GdkModifierType  state)
{
    GtkTextBuffer *buf = GTK_TEXT_BUFFER(silktex_editor_get_buffer(editor));

    /* --- Escape: deactivate any active snippet --- */
    if (keyval == GDK_KEY_Escape && self->active) {
        deactivate(self, editor);
        return TRUE;
    }

    /* --- Tab key --- */
    if (keyval == GDK_KEY_Tab) {
        /* 1. If snippet active, advance to next placeholder */
        if (self->active) {
            if (!goto_next(self->active, buf))
                deactivate(self, editor);
            return TRUE;
        }

        /* 2. Check if the word just before the cursor is a snippet key */
        GtkTextIter cursor, word_start;
        GtkTextMark *mark = gtk_text_buffer_get_insert(buf);
        gtk_text_buffer_get_iter_at_mark(buf, &cursor, mark);

        if (gtk_text_iter_ends_word(&cursor)) {
            word_start = cursor;
            gtk_text_iter_backward_word_start(&word_start);
            g_autofree char *word = gtk_text_iter_get_text(&word_start, &cursor);
            if (word && snippets_lookup(self, word)) {
                activate_snippet(self, editor, word);
                return TRUE;
            }
        }
    }

    /* --- Shift-Tab: go to previous placeholder --- */
    if (keyval == GDK_KEY_ISO_Left_Tab && (state & GDK_SHIFT_MASK)) {
        if (self->active) {
            if (!goto_prev(self->active, buf))
                deactivate(self, editor);
            return TRUE;
        }
    }

    /* --- Any other key while a snippet is active: sync mirrored groups --- */
    if (self->active && self->active->current) {
        SnippetHolder *active_h = self->active->current->data;
        GtkTextIter ls, le;
        gtk_text_buffer_get_iter_at_mark(buf, &ls, active_h->lmark);
        gtk_text_buffer_get_iter_at_mark(buf, &le, active_h->rmark);
        g_autofree char *active_text = gtk_text_iter_get_text(&ls, &le);

        /* Mirror to other members of the same group */
        GList *cur = self->active->holders;
        while (cur) {
            SnippetHolder *h = cur->data;
            if (h != active_h && h->group == active_h->group) {
                GtkTextIter ms, me;
                gtk_text_buffer_get_iter_at_mark(buf, &ms, h->lmark);
                gtk_text_buffer_get_iter_at_mark(buf, &me, h->rmark);
                gtk_text_buffer_delete(buf, &ms, &me);
                gtk_text_buffer_insert(buf, &ms, active_text, -1);
            }
            cur = cur->next;
        }
    }

    return FALSE;
}
