/*
 * SilkTex — unit tests for pure / side-effect-free C functions.
 *
 * Covers every public symbol that can be exercised without a live GTK
 * display.  Functions that require a window/editor/preview widget are
 * called interactively; see the comment block at the bottom of this file
 * for a complete interactive-test checklist.
 *
 * Build & run:
 *   ./run.sh --rebuild   # or: meson setup build-gtk4 && ninja -C build-gtk4
 *   ninja -C build-gtk4 test
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

/* ── headers for testable modules ─────────────────────────────────────── */
#include "../src/utils.h"
#include "../src/latex.h"
#include "../src/git.h"
#include "../src/snippets.h"
#include "../src/configfile.h"
#include "../src/collab.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  utils.c
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_in_debug_mode_default(void)
{
    slog_init(0);
    g_assert_false(in_debug_mode());
}

static void test_in_debug_mode_enabled(void)
{
    slog_init(1);
    g_assert_true(in_debug_mode());
    slog_init(0); /* reset */
}

static void test_slog_does_not_crash(void)
{
    slog_init(1);
    slog(L_INFO,    "info message\n");
    slog(L_DEBUG,   "debug message\n");
    slog(L_WARNING, "warning message\n");
    slog(L_ERROR,   "error message\n");
    /* L_FATAL calls exit() — skip */
    slog_init(0);
}

static void test_utils_path_exists_true(void)
{
    g_assert_true(utils_path_exists("/tmp"));
}

static void test_utils_path_exists_false(void)
{
    g_assert_false(utils_path_exists("/silktex_nonexistent_path_xyz"));
}

static void test_utils_path_exists_null(void)
{
    g_assert_false(utils_path_exists(NULL));
}

static void test_utils_copy_file(void)
{
    g_autofree char *src  = g_build_filename(g_get_tmp_dir(), "silktex_test_src.txt",  NULL);
    g_autofree char *dest = g_build_filename(g_get_tmp_dir(), "silktex_test_dest.txt", NULL);
    const char *content = "SilkTex copy test\n";

    g_file_set_contents(src, content, -1, NULL);

    GError *err = NULL;
    g_assert_true(utils_copy_file(src, dest, &err));
    g_assert_no_error(err);

    g_autofree char *got = NULL;
    gsize len = 0;
    g_file_get_contents(dest, &got, &len, NULL);
    g_assert_cmpstr(got, ==, content);

    g_unlink(src);
    g_unlink(dest);
}

static void test_utils_copy_file_missing_src(void)
{
    GError *err = NULL;
    g_assert_false(utils_copy_file("/silktex_no_such_file", "/tmp/silktex_dest", &err));
    g_assert_nonnull(err);
    g_error_free(err);
}

static void test_utils_subinstr_match(void)
{
    g_assert_true(utils_subinstr("ello", "Hello World", FALSE));
}

static void test_utils_subinstr_no_match(void)
{
    g_assert_false(utils_subinstr("xyz", "Hello World", FALSE));
}

static void test_utils_subinstr_case_insensitive(void)
{
    g_assert_true(utils_subinstr("HELLO", "hello world", TRUE));
}

static void test_utils_subinstr_case_sensitive_fail(void)
{
    g_assert_false(utils_subinstr("HELLO", "hello world", FALSE));
}

static void test_utils_subinstr_null(void)
{
    g_assert_false(utils_subinstr(NULL, "hello", FALSE));
    g_assert_false(utils_subinstr("hello", NULL, FALSE));
}

static void test_utils_subinstr_empty_needle(void)
{
    /* strstr(haystack, "") is always non-NULL by C standard */
    g_assert_true(utils_subinstr("", "hello", FALSE));
}

static void test_utils_subinstr_exact_match(void)
{
    g_assert_true(utils_subinstr("hello", "hello", FALSE));
}

static void test_utils_subinstr_empty_haystack(void)
{
    g_assert_false(utils_subinstr("hello", "", FALSE));
}

static void test_utils_subinstr_single_char(void)
{
    g_assert_true(utils_subinstr("H", "Hello", FALSE));
    g_assert_false(utils_subinstr("h", "Hello", FALSE));
    g_assert_true(utils_subinstr("h", "Hello", TRUE));
}

static void test_g_substr_basic(void)
{
    char *src = "Hello World";
    g_autofree char *s = g_substr(src, 0, 5);
    g_assert_cmpstr(s, ==, "Hello");
}

static void test_g_substr_middle(void)
{
    char *src = "Hello World";
    g_autofree char *s = g_substr(src, 6, 11);
    g_assert_cmpstr(s, ==, "World");
}

static void test_g_substr_empty_range(void)
{
    /* start == end: zero chars copied → empty string */
    g_autofree char *s = g_substr("Hello", 2, 2);
    g_assert_nonnull(s);
    g_assert_cmpstr(s, ==, "");
}

static void test_g_substr_full_string(void)
{
    char *src = "Hi";
    g_autofree char *s = g_substr(src, 0, 2);
    g_assert_cmpstr(s, ==, "Hi");
}

static slist *make_node(const char *first, const char *second)
{
    slist *n = g_new0(slist, 1);
    n->first  = g_strdup(first);
    n->second = g_strdup(second);
    return n;
}

static void free_slist(slist *head)
{
    while (head) {
        slist *next = head->next;
        g_free(head->first);
        g_free(head->second);
        g_free(head);
        head = next;
    }
}

static void test_slist_find_exact(void)
{
    slist *head = make_node("alpha", "1");
    slist *found = slist_find(head, "alpha", FALSE, FALSE);
    g_assert_nonnull(found);
    g_assert_cmpstr(found->first, ==, "alpha");
    g_assert_null(slist_find(head, "beta", FALSE, FALSE));
    free_slist(head);
}

static void test_slist_find_prefix(void)
{
    slist *head = make_node("gamma", "1");
    slist *found = slist_find(head, "gam", TRUE, FALSE);
    g_assert_nonnull(found);
    g_assert_cmpstr(found->first, ==, "gamma");
    g_assert_null(slist_find(head, "xyz", TRUE, FALSE));
    free_slist(head);
}

static void test_slist_find_create(void)
{
    slist *head = make_node("alpha", "1");
    slist *created = slist_find(head, "beta", FALSE, TRUE);
    g_assert_nonnull(created);
    g_assert_cmpstr(created->first, ==, "beta");
    /* must be findable now */
    g_assert_nonnull(slist_find(head, "beta", FALSE, FALSE));
    free_slist(head);
}

static void test_slist_find_null_list(void)
{
    g_assert_null(slist_find(NULL, "term", FALSE, FALSE));
    /* create=TRUE on NULL list: prev stays NULL, nothing created */
    g_assert_null(slist_find(NULL, "term", FALSE, TRUE));
}

static void test_slist_append_and_find(void)
{
    slist *head = make_node("a", "");
    slist *tail = make_node("b", "");
    head = slist_append(head, tail);
    g_assert_nonnull(slist_find(head, "b", FALSE, FALSE));
    free_slist(head);
}

static void test_slist_remove_head(void)
{
    slist *n1 = make_node("a", "");
    slist *n2 = make_node("b", "");
    n1->next = n2;
    slist *head = slist_remove(n1, n1);
    g_assert_true(head == n2);
    g_assert_null(slist_find(head, "a", FALSE, FALSE));
    free_slist(head);
    /* n1 was unlinked but not freed by slist_remove */
    g_free(n1->first); g_free(n1->second); g_free(n1);
}

static void test_slist_remove_tail(void)
{
    slist *n1 = make_node("a", "");
    slist *n2 = make_node("b", "");
    n1->next = n2;
    slist *head = slist_remove(n1, n2);
    g_assert_true(head == n1);
    g_assert_null(n1->next);
    free_slist(head);
    g_free(n2->first); g_free(n2->second); g_free(n2);
}

static void test_slist_remove_single_node(void)
{
    slist *node = make_node("only", "1");
    slist *result = slist_remove(node, node);
    g_assert_null(result);
    g_free(node->first); g_free(node->second); g_free(node);
}

static void test_slist_remove_null_list(void)
{
    slist dummy = {0};
    g_assert_null(slist_remove(NULL, &dummy));
}

/* ═══════════════════════════════════════════════════════════════════════
 *  latex.c — pure generators
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_generate_table_basic(void)
{
    g_autofree char *t = silktex_latex_generate_table(2, 3, 0, 1 /* center */);
    g_assert_nonnull(t);
    g_assert_nonnull(strstr(t, "\\begin{tabular}"));
    g_assert_nonnull(strstr(t, "\\end{tabular}"));
    g_assert_nonnull(strstr(t, "{ccc}"));   /* 3 centered cols, no borders */
    g_assert_nonnull(strstr(t, "12 & 13")); /* row 1, cols 2–3 */
}

static void test_generate_table_outer_borders(void)
{
    g_autofree char *t = silktex_latex_generate_table(2, 2, 1 /* outer */, 0 /* left */);
    g_assert_nonnull(t);
    g_assert_nonnull(strstr(t, "{|ll|}"));
    g_assert_nonnull(strstr(t, "\\hline"));
}

static void test_generate_table_all_borders(void)
{
    g_autofree char *t = silktex_latex_generate_table(2, 2, 2 /* all */, 2 /* right */);
    g_assert_nonnull(t);
    g_assert_nonnull(strstr(t, "{|r|r|}"));
}

static void test_generate_table_clamps_rows(void)
{
    /* rows=0 should clamp to 1 */
    g_autofree char *t = silktex_latex_generate_table(0, 2, 0, 1);
    g_assert_nonnull(t);
    g_assert_nonnull(strstr(t, "11"));
    g_assert_null(strstr(t, "21")); /* no second row */
}

static void test_generate_table_clamps_alignment(void)
{
    /* invalid alignment → centered */
    g_autofree char *t = silktex_latex_generate_table(1, 1, 0, 99);
    g_assert_nonnull(t);
    g_assert_nonnull(strstr(t, "{c}"));
}

static void test_generate_matrix_pmatrix(void)
{
    /* bracket=1 → pmatrix */
    g_autofree char *m = silktex_latex_generate_matrix(1, 2, 2);
    g_assert_nonnull(m);
    g_assert_nonnull(strstr(m, "\\begin{pmatrix}"));
    g_assert_nonnull(strstr(m, "\\end{pmatrix}"));
    g_assert_nonnull(strstr(m, "11 & 12"));
    g_assert_nonnull(strstr(m, "21 & 22"));
}

static void test_generate_matrix_bmatrix(void)
{
    g_autofree char *m = silktex_latex_generate_matrix(2, 1, 3);
    g_assert_nonnull(m);
    g_assert_nonnull(strstr(m, "\\begin{bmatrix}"));
    g_assert_nonnull(strstr(m, "11 & 12 & 13"));
}

static void test_generate_matrix_all_brackets(void)
{
    const char *names[] = {"matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix"};
    for (int i = 0; i < 6; i++) {
        g_autofree char *m = silktex_latex_generate_matrix(i, 1, 1);
        g_assert_nonnull(m);
        g_autofree char *begin = g_strdup_printf("\\begin{%s}", names[i]);
        g_assert_nonnull(strstr(m, begin));
    }
}

static void test_generate_matrix_clamps_bracket(void)
{
    /* out-of-range bracket → falls back to index 1 (pmatrix) */
    g_autofree char *m = silktex_latex_generate_matrix(99, 1, 1);
    g_assert_nonnull(m);
    g_assert_nonnull(strstr(m, "\\begin{pmatrix}"));
}

static void test_generate_image_basic(void)
{
    g_autofree char *img = silktex_latex_generate_image("fig.png", "My caption", "fig:test", 1.0);
    g_assert_nonnull(img);
    g_assert_nonnull(strstr(img, "\\begin{figure}"));
    g_assert_nonnull(strstr(img, "fig.png"));
    g_assert_nonnull(strstr(img, "My caption"));
    g_assert_nonnull(strstr(img, "fig:test"));
    g_assert_nonnull(strstr(img, "scale=1.00"));
}

static void test_generate_image_zero_scale(void)
{
    /* scale <= 0 → defaults to 1.0 */
    g_autofree char *img = silktex_latex_generate_image("x.pdf", "", "", 0.0);
    g_assert_nonnull(img);
    g_assert_nonnull(strstr(img, "scale=1.00"));
}

static void test_generate_image_null_args(void)
{
    /* NULL path/caption/label should not crash */
    g_autofree char *img = silktex_latex_generate_image(NULL, NULL, NULL, 0.5);
    g_assert_nonnull(img);
    g_assert_nonnull(strstr(img, "\\begin{figure}"));
}

static void test_generate_image_half_scale(void)
{
    g_autofree char *img = silktex_latex_generate_image("a.png", "c", "l", 0.5);
    g_assert_nonnull(img);
    g_assert_nonnull(strstr(img, "scale=0.50"));
}

/* ═══════════════════════════════════════════════════════════════════════
 *  git.c
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_git_is_available(void)
{
    /* git must be on PATH in any dev environment */
    g_assert_true(silktex_git_is_available());
}

static void test_git_status_load_non_repo(void)
{
    GError *err = NULL;
    SilktexGitStatus *s = silktex_git_status_load("/tmp", &err);
    /* /tmp is not a git repo — should return NULL with an error */
    if (s) {
        silktex_git_status_free(s);
    } else {
        g_clear_error(&err);
    }
}

static void test_git_status_load_real_repo(void)
{
    /* Run from the SilkTex repo root */
    g_autofree char *repo = g_get_current_dir();
    GError *err = NULL;
    SilktexGitStatus *s = silktex_git_status_load(repo, &err);
    if (s) {
        /* branch and files arrays exist */
        g_assert_nonnull(s->branch);
        silktex_git_status_free(s);
    } else {
        /* tolerate if the test is run outside the repo */
        g_clear_error(&err);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  snippets.c — GObject lifecycle (no display needed)
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_snippets_new(void)
{
    SilktexSnippets *s = silktex_snippets_new();
    g_assert_nonnull(s);
    g_object_unref(s);
}

static void test_snippets_get_filename(void)
{
    SilktexSnippets *s = silktex_snippets_new();
    const char *fname = silktex_snippets_get_filename(s);
    g_assert_nonnull(fname);
    g_assert_true(g_str_has_suffix(fname, ".json"));
    g_object_unref(s);
}

static void test_snippets_reload(void)
{
    SilktexSnippets *s = silktex_snippets_new();
    silktex_snippets_reload(s); /* should not crash */
    g_object_unref(s);
}

static void test_snippets_reset_to_default(void)
{
    SilktexSnippets *s = silktex_snippets_new();
    silktex_snippets_reset_to_default(s); /* copies bundled snippets.json */
    g_object_unref(s);
}

static void test_snippets_set_modifiers(void)
{
    SilktexSnippets *s = silktex_snippets_new();
    silktex_snippets_set_modifiers(s, "Shift", "Alt");
    silktex_snippets_set_modifiers(s, "", "");
    silktex_snippets_set_modifiers(s, NULL, NULL);
    g_object_unref(s);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  configfile.c — roundtrip tests
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_config_string_roundtrip(void)
{
    config_init();
    config_set_string("Test", "key_str", "hello");
    g_assert_cmpstr(config_get_string("Test", "key_str"), ==, "hello");
    config_set_string("Test", "key_str", "world");
    g_assert_cmpstr(config_get_string("Test", "key_str"), ==, "world");
}

static void test_config_boolean_roundtrip(void)
{
    config_init();
    config_set_boolean("Test", "key_bool", TRUE);
    g_assert_true(config_get_boolean("Test", "key_bool"));
    config_set_boolean("Test", "key_bool", FALSE);
    g_assert_false(config_get_boolean("Test", "key_bool"));
}

static void test_config_integer_roundtrip(void)
{
    config_init();
    config_set_integer("Test", "key_int", 42);
    g_assert_cmpint(config_get_integer("Test", "key_int"), ==, 42);
    config_set_integer("Test", "key_int", -7);
    g_assert_cmpint(config_get_integer("Test", "key_int"), ==, -7);
}

static void test_config_missing_key_defaults(void)
{
    config_init();
    /* missing key → empty string / 0 / FALSE */
    const char *s = config_get_string("Test", "no_such_key_xyz");
    g_assert_true(s == NULL || *s == '\0');
    g_assert_false(config_get_boolean("Test", "no_such_bool_xyz"));
    g_assert_cmpint(config_get_integer("Test", "no_such_int_xyz"), ==, 0);
}

static void test_config_overwrite(void)
{
    config_init();
    config_set_string("TestOvr", "k", "first");
    config_set_string("TestOvr", "k", "second");
    g_assert_cmpstr(config_get_string("TestOvr", "k"), ==, "second");
}

static void test_config_isolation_across_groups(void)
{
    config_init();
    config_set_string("GroupA", "shared", "valueA");
    config_set_string("GroupB", "shared", "valueB");
    g_assert_cmpstr(config_get_string("GroupA", "shared"), ==, "valueA");
    g_assert_cmpstr(config_get_string("GroupB", "shared"), ==, "valueB");
}

static void test_config_many_keys(void)
{
    /* Write 64 integer keys, read them all back */
    config_init();
    for (int i = 0; i < 64; i++) {
        g_autofree char *k = g_strdup_printf("key_%d", i);
        config_set_integer("StressGroup", k, i * 7);
    }
    for (int i = 0; i < 64; i++) {
        g_autofree char *k = g_strdup_printf("key_%d", i);
        g_assert_cmpint(config_get_integer("StressGroup", k), ==, i * 7);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  memory / lifecycle stress
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_snippets_lifecycle_stress(void)
{
    /* 20 create/unref cycles — detects leaks and use-after-free under
     * G_SLICE=always-malloc + G_DEBUG=gc-friendly */
    for (int i = 0; i < 20; i++) {
        SilktexSnippets *s = silktex_snippets_new();
        g_assert_nonnull(s);
        silktex_snippets_set_modifiers(s, "Shift", "Alt");
        g_object_unref(s);
    }
}

static void test_git_status_free_null(void)
{
    /* NULL guard must hold — silktex_git_status_free has an explicit check */
    silktex_git_status_free(NULL);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  hostile-input tests
 *
 *  Cases that are known (or suspected) to crash run in a child process via
 *  RUN_IN_SUBPROCESS_OR_SKIP: a crash/abort in the child marks the test as
 *  skipped with a "BUG:" note instead of taking down the whole run, and the
 *  test starts passing automatically once src/ is fixed.
 * ═══════════════════════════════════════════════════════════════════════ */

#define RUN_IN_SUBPROCESS_OR_SKIP(bug_note)                         \
    if (!g_test_subprocess()) {                                     \
        g_test_trap_subprocess(NULL, 0, 0);                         \
        if (!g_test_trap_has_passed()) g_test_skip(bug_note);       \
        return;                                                     \
    }

static char *silktex_conf_path(const char *basename)
{
    return g_build_filename(g_get_user_config_dir(), "silktex", basename, NULL);
}

static void write_conf_file(const char *basename, const char *data, gssize len)
{
    g_autofree char *dir = silktex_conf_path(NULL);
    g_mkdir_with_parents(dir, 0700);
    g_autofree char *path = silktex_conf_path(basename);
    g_assert_true(g_file_set_contents(path, data, len, NULL));
}

/* ── test isolation ───────────────────────────────────────────────────── */

static void test_isolation_uses_private_xdg_dir(void)
{
    /* main() points XDG_CONFIG_HOME at a private temp dir; everything the
     * modules write must land there, never in the real ~/.config. */
    const char *cfg = g_get_user_config_dir();
    g_assert_true(g_str_has_prefix(cfg, g_get_tmp_dir()));
    g_assert_null(strstr(cfg, "/.config"));
    config_init();
    g_autofree char *ini = silktex_conf_path("silktex.ini");
    g_assert_true(g_file_test(ini, G_FILE_TEST_IS_REGULAR));
}

/* ── utils.c: g_substr ────────────────────────────────────────────────── */

static void test_g_substr_nul_terminated(void)
{
    char *src = "Hello World";
    g_autofree char *s = g_substr(src, 0, 5);
    g_assert_cmpuint(strlen(s), ==, 5);
    g_assert_cmpint(s[5], ==, '\0');
}

static void test_g_substr_end_beyond_length(void)
{
    char *src = "Hi";
    g_autofree char *s = g_substr(src, 0, 40);
    g_assert_nonnull(s);
    g_assert_cmpstr(s, ==, "Hi");
}

static void test_g_substr_start_gt_end(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: utils.c:111 g_substr(start>end): len=end-start+1 is <=0, "
                              "g_malloc0(0) returns NULL and strncpy(NULL, ., (size_t)-1) segfaults");
    char *src = "Hello";
    g_autofree char *s = g_substr(src, 3, 2);
    g_assert_nonnull(s);
    g_assert_cmpstr(s, ==, "");
}

static void test_g_substr_negative_end(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: utils.c:111 g_substr(0, -1): g_malloc0(0)=NULL then "
                              "strncpy with (size_t)-1 bytes segfaults");
    char *src = "Hello";
    g_autofree char *s = g_substr(src, 0, -1);
    g_assert_nonnull(s);
    g_assert_cmpstr(s, ==, "");
}

static void test_g_substr_negative_start(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: utils.c:111 g_substr(-1, 2): reads src[-1] (out of bounds)");
    char buf[] = "XHello"; /* guard byte so src[-1] is defined memory */
    g_autofree char *s = g_substr(buf + 1, -1, 2);
    g_assert_nonnull(s);
    /* Whatever the policy, the result must never contain memory before src. */
    g_assert_cmpint(s[0], !=, 'X');
}

/* ── utils.c: slist ───────────────────────────────────────────────────── */

static void test_slist_find_null_term_exact(void)
{
    slist *head = make_node("alpha", "1");
    g_assert_null(slist_find(head, NULL, FALSE, FALSE));
    free_slist(head);
}

static void test_slist_find_null_term_prefix(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: utils.c:130 slist_find(term=NULL, n=TRUE) calls strlen(NULL)");
    slist *head = make_node("alpha", "1");
    g_assert_null(slist_find(head, NULL, TRUE, FALSE));
    free_slist(head);
}

static void test_slist_find_null_first_exact(void)
{
    slist *head = g_new0(slist, 1); /* first == NULL */
    g_assert_null(slist_find(head, "x", FALSE, FALSE));
    g_free(head);
}

static void test_slist_find_null_first_prefix(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: utils.c:130 slist_find(n=TRUE) on a node whose first==NULL "
                              "calls strncmp(NULL, ...)");
    slist *head = g_new0(slist, 1);
    g_assert_null(slist_find(head, "x", TRUE, FALSE));
    g_free(head);
}

static void test_slist_find_empty_prefix_matches_head(void)
{
    slist *head = make_node("alpha", "1");
    g_assert_true(slist_find(head, "", TRUE, FALSE) == head);
    free_slist(head);
}

static void test_slist_append_null_head(void)
{
    /* Appending to an empty list makes the node the new head. */
    slist *node = make_node("a", "");
    g_assert_true(slist_append(NULL, node) == node);
    g_assert_null(node->next);
    free_slist(node);
}

static void test_slist_append_null_node(void)
{
    slist *head = make_node("a", "");
    g_assert_true(slist_append(head, NULL) == head);
    g_assert_null(head->next);
    free_slist(head);
}

static void test_slist_remove_null_node(void)
{
    slist *head = make_node("a", "");
    g_assert_true(slist_remove(head, NULL) == head);
    free_slist(head);
}

static void test_slist_remove_foreign_node(void)
{
    slist *head = make_node("a", "");
    slist *other = make_node("b", "");
    g_assert_true(slist_remove(head, other) == head);
    g_assert_null(head->next);
    free_slist(head);
    free_slist(other);
}

/* ── latex.c: hostile counts and text ─────────────────────────────────── */

static int count_occurrences(const char *hay, const char *needle)
{
    int n = 0;
    size_t nl = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + nl, needle)) n++;
    return n;
}

static void test_generate_table_negative_counts(void)
{
    g_autofree char *t = silktex_latex_generate_table(-5, -3, -1, -1);
    g_assert_nonnull(t);
    g_assert_nonnull(strstr(t, "{c}"));
    g_assert_cmpint(count_occurrences(t, "\\\\"), ==, 1);
    g_assert_null(strstr(t, "\\hline"));
    g_assert_null(strstr(t, "&"));
}

static void test_generate_table_large(void)
{
    /* Oversized counts are clamped to 99 (append_int2 emits two digits). */
    const int rows = 99, cols = 40;
    g_autofree char *t = silktex_latex_generate_table(500, cols, 2, 0);
    g_assert_nonnull(t);
    g_assert_cmpint(count_occurrences(t, "\\\\"), ==, rows);
    g_assert_cmpint(count_occurrences(t, " & "), ==, rows * (cols - 1));
    g_assert_cmpint(count_occurrences(t, "\\hline"), ==, rows + 1);
    g_assert_true(g_str_has_suffix(t, "\\end{tabular}\n"));
}

static void test_generate_matrix_negative_counts(void)
{
    g_autofree char *m = silktex_latex_generate_matrix(-1, -1, -1);
    g_assert_nonnull(m);
    g_assert_nonnull(strstr(m, "\\begin{pmatrix}"));
    g_assert_cmpint(count_occurrences(m, "\\\\"), ==, 1);
    g_assert_null(strstr(m, "&"));
}

static void test_generate_matrix_large(void)
{
    /* Oversized counts are clamped to 99 (append_int2 emits two digits). */
    const int rows = 99, cols = 30;
    g_autofree char *m = silktex_latex_generate_matrix(0, 300, cols);
    g_assert_nonnull(m);
    g_assert_cmpint(count_occurrences(m, "\\\\"), ==, rows);
    g_assert_cmpint(count_occurrences(m, " & "), ==, rows * (cols - 1));
    g_assert_true(g_str_has_suffix(m, "\\end{matrix}$\n"));
}

static void test_generate_image_format_chars(void)
{
    /* printf directives and LaTeX specials in user text must pass through
     * verbatim (never be interpreted as a format string). */
    const char *path = "dir/%s%n%d/img{1}.png";
    const char *cap  = "50% done \\emph{x} }{";
    const char *lbl  = "fig:%p}";
    g_autofree char *img = silktex_latex_generate_image(path, cap, lbl, 1.0);
    g_assert_nonnull(img);
    g_assert_nonnull(strstr(img, "{dir/%s%n%d/img{1}.png}"));
    g_assert_nonnull(strstr(img, "\\caption{50% done \\emph{x} }{}"));
    g_assert_nonnull(strstr(img, "\\label{fig:%p}}"));
}

static void test_generate_image_negative_scale(void)
{
    g_autofree char *img = silktex_latex_generate_image("a.png", "", "", -3.0);
    g_assert_nonnull(strstr(img, "scale=1.00"));
}

static void test_generate_image_huge_scale(void)
{
    g_autofree char *img = silktex_latex_generate_image("a.png", "", "", 1e12);
    g_assert_nonnull(strstr(img, "scale=1000000000000.00"));
}

static void test_generate_image_empty_strings(void)
{
    g_autofree char *img = silktex_latex_generate_image("", "", "", 1.0);
    g_assert_nonnull(strstr(img, "{}\n\\caption{}\n\\label{}"));
}

/* ── snippets.c: hostile snippets.json ────────────────────────────────── */

static void snippets_load_and_reset(void)
{
    SilktexSnippets *s = silktex_snippets_new();
    g_assert_nonnull(s);
    silktex_snippets_reload(s);
    silktex_snippets_set_modifiers(s, "Shift", "Alt");
    silktex_snippets_reset_to_default(s);
    g_object_unref(s);
}

static void test_snippets_json_garbage(void)
{
    write_conf_file("snippets.json", "{{{{ not json at all ]]] \"", -1);
    snippets_load_and_reset();
}

static void test_snippets_json_empty_file(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: snippets.c:251 load_json_snippets: json_parser_get_root() "
                              "returns NULL for an empty file and JSON_NODE_HOLDS_OBJECT(NULL) "
                              "hits a json-glib critical");
    write_conf_file("snippets.json", "", 0);
    snippets_load_and_reset();
}

static void test_snippets_json_root_not_object(void)
{
    write_conf_file("snippets.json", "[1, 2, \"three\", null]", -1);
    snippets_load_and_reset();
}

static void test_snippets_json_non_utf8(void)
{
    static const char raw[] = "{\"x\": {\"prefix\": \"\xff\xfe\", \"body\": \"\xc3\", "
                              "\"description\": \"\x80\x80\"}}";
    write_conf_file("snippets.json", raw, sizeof raw - 1);
    snippets_load_and_reset();
}

static void test_snippets_json_edge_bodies(void)
{
    write_conf_file("snippets.json",
        "{"
        "\"a\":{\"prefix\":\"a\",\"body\":\"$\"},"
        "\"b\":{\"prefix\":\"b\",\"body\":\"${\"},"
        "\"c\":{\"prefix\":\"c\",\"body\":\"${1\"},"
        "\"d\":{\"prefix\":\"d\",\"body\":\"${1:\"},"
        "\"e\":{\"prefix\":\"e\",\"body\":\"$999999999999\"},"
        "\"f\":{\"prefix\":\"f\",\"body\":\"$-1\"},"
        "\"g\":{\"prefix\":\"g\",\"body\":\"${0:$1}\"},"
        "\"h\":{\"prefix\":\"h\",\"body\":\"${1:${2:${3:${4:${5:${6:${7:${8:x}}}}}}}}\"},"
        "\"i\":{\"prefix\":\"i\",\"body\":\"\"},"
        "\"j\":{\"prefix\":\"j\",\"body\":[]},"
        "\"k\":{\"prefix\":[],\"body\":[\"$1\",\"$2\"]},"
        "\"l\":{\"prefix\":\"\",\"body\":\"x\",\"accelerator\":\"\"},"
        "\"m\":{\"prefix\":\"m\",\"body\":\"x\",\"accelerator\":\"<<>><Shift>>>zz\"},"
        "\"n\":{},"
        "\"o\":{\"prefix\":\"o\",\"body\":\"x\",\"accelerator\":\"abcdefghijklmnopqrstuvwxyz\"}"
        "}", -1);
    snippets_load_and_reset();
}

static void test_snippets_json_entry_not_object(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: snippets.c:240 load_json_snippets calls "
                              "json_object_get_object_member() on a non-object member "
                              "-> json-glib critical (fatal under g_test / G_DEBUG=fatal-criticals)");
    write_conf_file("snippets.json", "{\"foo\": \"bar\", \"n\": 5, \"z\": null, \"arr\": [1]}", -1);
    snippets_load_and_reset();
}

static void test_snippets_json_description_not_string(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: snippets.c:243-248 load_json_snippets calls "
                              "json_object_get_string_member() on non-string "
                              "description/accelerator -> json-glib critical");
    write_conf_file("snippets.json",
                    "{\"x\": {\"prefix\": \"x\", \"body\": \"b\", \"description\": 5, "
                    "\"accelerator\": [1]}, \"y\": {\"prefix\": 7, \"body\": {\"k\": 1}, "
                    "\"description\": null, \"accelerator\": null}}", -1);
    snippets_load_and_reset();
}

static void test_snippets_json_body_array_non_string(void)
{
    RUN_IN_SUBPROCESS_OR_SKIP("BUG: snippets.c:190 snippet_body_from_json_member calls "
                              "json_array_get_string_element() on a non-string array element "
                              "-> json-glib critical");
    write_conf_file("snippets.json",
                    "{\"x\": {\"prefix\": \"x\", \"body\": [\"line\", 42, null, {\"o\": 1}, [2]]}}",
                    -1);
    snippets_load_and_reset();
}

/* ── configfile.c: corrupted ini / hostile integers ───────────────────── */

static void test_config_garbage_ini(void)
{
    static const char raw[] = "\x00\xff\xfe[[[ garbage ==\n=\n[\n]]]\nkey\n\x01\x02";
    write_conf_file("silktex.ini", raw, sizeof raw - 1);
    config_init();
    /* unreadable file → defaults are loaded and written back */
    g_assert_cmpint(config_get_integer("Interface", "mainwindow_w"), ==, 1200);
    g_assert_cmpstr(config_get_string("Compile", "typesetter"), ==, "pdflatex");
}

static void test_config_truncated_ini(void)
{
    write_conf_file("silktex.ini", "[Interface]\nmainwindow_w = 640\nmainwindow_h", -1);
    config_init();
    /* either the file was rejected (defaults) or partially read; no crash,
     * and every accessor still returns a sane value */
    gint w = config_get_integer("Interface", "mainwindow_w");
    g_assert_true(w == 640 || w == 1200);
    g_assert_cmpint(config_get_integer("Interface", "mainwindow_h"), >=, 0);
}

static void test_config_integer_extremes(void)
{
    write_conf_file("silktex.ini",
                    "[General]\nconfig_version = 1.0.3\n"
                    "[Ints]\n"
                    "big = 99999999999999999999\n"
                    "neg = -99999999999999999999\n"
                    "max = 2147483647\n"
                    "min = -2147483648\n"
                    "over = 2147483648\n"
                    "under = -2147483649\n"
                    "flt = 1.5\n"
                    "hex = 0x10\n"
                    "txt = twelve\n"
                    "empty =\n"
                    "spaces =    42   \n", -1);
    config_init();
    g_assert_cmpint(config_get_integer("Ints", "big"),    ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "neg"),    ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "max"),    ==, G_MAXINT);
    g_assert_cmpint(config_get_integer("Ints", "min"),    ==, G_MININT);
    g_assert_cmpint(config_get_integer("Ints", "over"),   ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "under"),  ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "flt"),    ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "hex"),    ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "txt"),    ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "empty"),  ==, 0);
    g_assert_cmpint(config_get_integer("Ints", "spaces"), ==, 42);
    /* booleans from garbage → FALSE, strings → verbatim */
    g_assert_false(config_get_boolean("Ints", "big"));
    g_assert_cmpstr(config_get_string("Ints", "txt"), ==, "twelve");
}

static void test_config_integer_roundtrip_extremes(void)
{
    config_init();
    config_set_integer("Ext", "max", G_MAXINT);
    config_set_integer("Ext", "min", G_MININT);
    config_save();
    config_init(); /* reload from disk */
    g_assert_cmpint(config_get_integer("Ext", "max"), ==, G_MAXINT);
    g_assert_cmpint(config_get_integer("Ext", "min"), ==, G_MININT);
}

static void test_config_string_specials_roundtrip(void)
{
    config_init();
    const char *v = "a\nb=c;#[x]\t\\n\"quoted\" %s %% \xc3\xa9";
    config_set_string("Spec", "k", v);
    config_save();
    config_init();
    g_assert_cmpstr(config_get_string("Spec", "k"), ==, v);
}

static void test_config_missing_group(void)
{
    config_init();
    g_assert_cmpstr(config_get_string("NoSuchGroup", "k"), ==, "");
    g_assert_false(config_get_boolean("NoSuchGroup", "k"));
    g_assert_cmpint(config_get_integer("NoSuchGroup", "k"), ==, 0);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  collab.c — silktex_collab_transform_op()
 *
 *  A remote op is (retain, delete) plus text inserted at `retain`; a local
 *  op is (retain, insert_len, delete_len).  The transform rewrites the
 *  remote op so it still means the same thing on a buffer that already has
 *  the local op applied.  Every case below is checked against the policy
 *  documented in collab.h: ties place the local text first, and a delete
 *  swallows an insertion that lands strictly inside it.
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── no concurrency: an empty local op must be the identity ───────────── */

static void test_collab_tf_identity_no_local_op(void)
{
    int r = 7, d = 3;
    g_assert_true(silktex_collab_transform_op(&r, &d, 0, 0, 0));
    g_assert_cmpint(r, ==, 7);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_identity_zero_len_local_at_same_offset(void)
{
    int r = 4, d = 2;
    g_assert_true(silktex_collab_transform_op(&r, &d, 4, 0, 0));
    g_assert_cmpint(r, ==, 4);
    g_assert_cmpint(d, ==, 2);
}

static void test_collab_tf_null_pointers_tolerated(void)
{
    int d = 1;
    g_assert_true(silktex_collab_transform_op(NULL, &d, 0, 3, 0));
    g_assert_cmpint(d, ==, 1);
    int r = 1;
    g_assert_true(silktex_collab_transform_op(&r, NULL, 0, 3, 0));
    g_assert_cmpint(r, ==, 1);
}

/* ── local insert ─────────────────────────────────────────────────────── */

static void test_collab_tf_insert_before_shifts_retain(void)
{
    int r = 10, d = 4;
    g_assert_true(silktex_collab_transform_op(&r, &d, 2, 3, 0));
    g_assert_cmpint(r, ==, 13);
    g_assert_cmpint(d, ==, 4);
}

static void test_collab_tf_insert_after_is_noop(void)
{
    int r = 2, d = 3;   /* remote covers [2,5) */
    g_assert_true(silktex_collab_transform_op(&r, &d, 5, 4, 0));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_insert_at_remote_start_shifts(void)
{
    /* Tie: the local text goes first, so the remote range starts after it. */
    int r = 5, d = 2;
    g_assert_true(silktex_collab_transform_op(&r, &d, 5, 3, 0));
    g_assert_cmpint(r, ==, 8);
    g_assert_cmpint(d, ==, 2);
}

static void test_collab_tf_insert_at_remote_end_is_noop(void)
{
    /* Adjacent, not inside: [2,5) with the insert exactly at 5. */
    int r = 2, d = 3;
    g_assert_true(silktex_collab_transform_op(&r, &d, 5, 1, 0));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_insert_inside_remote_delete_grows_it(void)
{
    /* Remote deletes [2,6); the local insert of 3 chars lands at 4 and is
     * swallowed by that delete. */
    int r = 2, d = 4;
    g_assert_true(silktex_collab_transform_op(&r, &d, 4, 3, 0));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 7);
}

static void test_collab_tf_insert_before_zero_width_remote(void)
{
    int r = 5, d = 0;
    g_assert_true(silktex_collab_transform_op(&r, &d, 5, 2, 0));
    g_assert_cmpint(r, ==, 7);
    g_assert_cmpint(d, ==, 0);
}

/* ── local delete ─────────────────────────────────────────────────────── */

static void test_collab_tf_delete_before_shifts_retain_back(void)
{
    int r = 10, d = 3;
    g_assert_true(silktex_collab_transform_op(&r, &d, 2, 0, 4)); /* removes [2,6) */
    g_assert_cmpint(r, ==, 6);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_delete_ending_exactly_at_remote_start(void)
{
    int r = 6, d = 3;
    g_assert_true(silktex_collab_transform_op(&r, &d, 2, 0, 4)); /* removes [2,6) */
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_delete_after_is_noop(void)
{
    int r = 2, d = 3;   /* remote covers [2,5) */
    g_assert_true(silktex_collab_transform_op(&r, &d, 5, 0, 4));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_delete_overlaps_remote_head(void)
{
    /* local removes [2,6), remote covers [4,9): 2 chars of overlap, and the
     * remote start clamps to the local delete's start.  The remote insertion
     * point (4) was strictly inside the local delete, so it does not survive. */
    int r = 4, d = 5;
    g_assert_false(silktex_collab_transform_op(&r, &d, 2, 0, 4));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 3);
}

static void test_collab_tf_delete_overlaps_remote_tail(void)
{
    /* local removes [6,10), remote covers [4,9): 3 chars of overlap. */
    int r = 4, d = 5;
    g_assert_true(silktex_collab_transform_op(&r, &d, 6, 0, 4));
    g_assert_cmpint(r, ==, 4);
    g_assert_cmpint(d, ==, 2);
}

static void test_collab_tf_delete_contains_remote_range(void)
{
    /* local removes [2,10), remote covers [4,7): nothing left to delete, and
     * the remote insertion point at 4 is swallowed by the local delete. */
    int r = 4, d = 3;
    g_assert_false(silktex_collab_transform_op(&r, &d, 2, 0, 8));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 0);
}

static void test_collab_tf_delete_inside_remote_range(void)
{
    /* local removes [4,6), remote covers [2,10). */
    int r = 2, d = 8;
    g_assert_true(silktex_collab_transform_op(&r, &d, 4, 0, 2));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 6);
}

static void test_collab_tf_identical_deletes_cancel(void)
{
    int r = 3, d = 4;
    g_assert_true(silktex_collab_transform_op(&r, &d, 3, 0, 4));
    g_assert_cmpint(r, ==, 3);
    g_assert_cmpint(d, ==, 0);
}

/* ── the remote insertion point vs. a local delete ────────────────────── */

static void test_collab_tf_remote_insert_inside_local_delete_dropped(void)
{
    /* local removes [2,8); the remote inserts at 5 — strictly inside. */
    int r = 5, d = 0;
    g_assert_false(silktex_collab_transform_op(&r, &d, 2, 0, 6));
    g_assert_cmpint(r, ==, 2);
    g_assert_cmpint(d, ==, 0);
}

static void test_collab_tf_remote_insert_at_local_delete_start_survives(void)
{
    int r = 2, d = 0;
    g_assert_true(silktex_collab_transform_op(&r, &d, 2, 0, 6));
    g_assert_cmpint(r, ==, 2);
}

static void test_collab_tf_remote_insert_at_local_delete_end_survives(void)
{
    int r = 8, d = 0;
    g_assert_true(silktex_collab_transform_op(&r, &d, 2, 0, 6));
    g_assert_cmpint(r, ==, 2);
}

static void test_collab_tf_remote_insert_survives_local_insert(void)
{
    int r = 5, d = 0;
    g_assert_true(silktex_collab_transform_op(&r, &d, 3, 9, 0));
    g_assert_cmpint(r, ==, 14);
}

/* ── clamping / hostile input ─────────────────────────────────────────── */

static void test_collab_tf_never_returns_negative(void)
{
    int r = -5, d = -5;
    silktex_collab_transform_op(&r, &d, -3, -1, -1);
    g_assert_cmpint(r, >=, 0);
    g_assert_cmpint(d, >=, 0);
}

static void test_collab_tf_negative_local_treated_as_zero(void)
{
    int r = 4, d = 2;
    g_assert_true(silktex_collab_transform_op(&r, &d, -100, -100, -100));
    g_assert_cmpint(r, ==, 4);
    g_assert_cmpint(d, ==, 2);
}

static void test_collab_tf_huge_values_stay_in_range(void)
{
    int r = G_MAXINT, d = G_MAXINT;
    silktex_collab_transform_op(&r, &d, G_MAXINT, G_MAXINT, G_MAXINT);
    g_assert_cmpint(r, >=, 0);
    g_assert_cmpint(d, >=, 0);
    g_assert_cmpint(r, <=, G_MAXINT / 4);
    g_assert_cmpint(d, <=, G_MAXINT / 4);
}

/* ── randomized convergence property ──────────────────────────────────── */

/*
 * A generic op: delete `del` characters at `pos`, then insert `ins` there.
 * The production transform rewrites a remote op past a local one; the mirror
 * below rewrites a local op past a remote one, using the same tie-break the
 * other way round (local text wins an equal-offset tie).  If the pair is a
 * correct transform, applying local-then-remote' and remote-then-local' to
 * the same starting string must land on the same final string.
 */
typedef struct {
    int  pos;
    int  del;
    char ins[8];   /* NUL-terminated ASCII, so chars == bytes */
} OtOp;

static void ot_apply(GString *s, const OtOp *op)
{
    g_assert_cmpint(op->pos, >=, 0);
    g_assert_cmpint(op->del, >=, 0);
    g_assert_cmpint(op->pos + op->del, <=, (int)s->len);
    if (op->del > 0)  g_string_erase(s, op->pos, op->del);
    if (op->ins[0])   g_string_insert(s, op->pos, op->ins);
}

/* Mirror of silktex_collab_transform_op: transform `a` past `b`, where `a`
 * wins an equal-offset insertion tie (the production function's `a` loses). */
static void ot_transform_local(OtOp *a, const OtOp *b)
{
    int a_lo  = a->pos, a_hi = a->pos + a->del;
    int b_lo  = b->pos, b_hi = b->pos + b->del;
    int b_ins = (int)strlen(b->ins);

    /* Same policy as the production side: a delete wins over an insertion
     * that lands strictly inside it. */
    if (b->del > 0 && b_lo < a_lo && a_lo < b_hi)
        a->ins[0] = '\0';

    int overlap = MIN(a_hi, b_hi) - MAX(a_lo, b_lo);
    if (overlap < 0) overlap = 0;
    int del = a->del - overlap;
    if (del < 0) del = 0;

    /* Every comparison is on the ORIGINAL offsets — both ops describe the
     * same starting string, so that is the one space they share. */
    int pos;
    if (a_lo < b_lo) {
        pos = a_lo;
        if (b_ins > 0 && b_lo < a_hi) del += b_ins;  /* b's text lands inside a's delete */
    } else if (a_lo == b_lo) {
        /* Equal offsets: the local op wins, so its insertion stays in front
         * of b's — but a local *delete* still has to step over b's text. */
        pos = a_lo + (a->del > 0 ? b_ins : 0);
    } else {
        pos = a_lo - MIN(b->del, a_lo - b_lo) + b_ins;
    }

    a->pos = pos;
    a->del = del;
}

static void ot_random_string(GString *s, const char *alphabet, int len)
{
    g_string_truncate(s, 0);
    for (int i = 0; i < len; i++)
        g_string_append_c(s, alphabet[g_random_int_range(0, (int)strlen(alphabet))]);
}

static void test_collab_tf_random_convergence(void)
{
    /* Deterministic: a failure here reproduces on the next run. */
    g_random_set_seed(0x5117E7);

    GString *base = g_string_new(NULL);
    GString *lhs  = g_string_new(NULL);
    GString *rhs  = g_string_new(NULL);
    GString *tmp  = g_string_new(NULL);

    for (int iter = 0; iter < 20000; iter++) {
        int len = g_random_int_range(0, 13);
        ot_random_string(base, "abcdef", len);

        /* The local op is what a GtkTextBuffer signal produces: either a pure
         * insert or a pure delete, never both. */
        OtOp local = { 0, 0, { 0 } };
        local.pos = g_random_int_range(0, len + 1);
        if (g_random_boolean()) {
            ot_random_string(tmp, "XYZ", g_random_int_range(1, 4));
            g_strlcpy(local.ins, tmp->str, sizeof local.ins);
        } else {
            local.del = g_random_int_range(0, len - local.pos + 1);
        }

        /* A remote op comes from a whole-document diff, so it may replace:
         * delete and insert at the same offset. */
        OtOp remote = { 0, 0, { 0 } };
        remote.pos = g_random_int_range(0, len + 1);
        remote.del = g_random_int_range(0, len - remote.pos + 1);
        int rins = g_random_int_range(0, 4);
        if (rins > 0) {
            ot_random_string(tmp, "PQR", rins);
            g_strlcpy(remote.ins, tmp->str, sizeof remote.ins);
        }

        /* local, then the remote op transformed past it. */
        OtOp r_prime = remote;
        if (!silktex_collab_transform_op(&r_prime.pos, &r_prime.del,
                                         local.pos, (int)strlen(local.ins), local.del))
            r_prime.ins[0] = '\0';
        g_string_assign(lhs, base->str);
        ot_apply(lhs, &local);
        ot_apply(lhs, &r_prime);

        /* remote, then the local op transformed past it. */
        OtOp l_prime = local;
        ot_transform_local(&l_prime, &remote);
        g_string_assign(rhs, base->str);
        ot_apply(rhs, &remote);
        ot_apply(rhs, &l_prime);

        if (!g_str_equal(lhs->str, rhs->str)) {
            g_error("iteration %d diverged: base=\"%s\" "
                    "local=(%d,%d,\"%s\") remote=(%d,%d,\"%s\") "
                    "=> \"%s\" vs \"%s\"",
                    iter, base->str,
                    local.pos, local.del, local.ins,
                    remote.pos, remote.del, remote.ins,
                    lhs->str, rhs->str);
        }
    }

    g_string_free(base, TRUE);
    g_string_free(lhs,  TRUE);
    g_string_free(rhs,  TRUE);
    g_string_free(tmp,  TRUE);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  test isolation
 *
 *  configfile.c / snippets.c write under g_get_user_config_dir()/silktex.
 *  Point XDG_CONFIG_HOME (and XDG_CACHE_HOME) at a private temp dir before
 *  GLib caches the value, and remove it at exit.  Subprocess re-executions
 *  (g_test_trap_subprocess) inherit the parent's dir and leave cleanup to
 *  the parent.
 * ═══════════════════════════════════════════════════════════════════════ */

static char *test_xdg_dir = NULL;

static void rmtree(const char *path)
{
    GDir *d = g_dir_open(path, 0, NULL);
    if (d) {
        const char *name;
        while ((name = g_dir_read_name(d))) {
            g_autofree char *child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR) && !g_file_test(child, G_FILE_TEST_IS_SYMLINK))
                rmtree(child);
            else
                g_unlink(child);
        }
        g_dir_close(d);
    }
    g_rmdir(path);
}

static void remove_test_xdg_dir(void)
{
    if (test_xdg_dir) rmtree(test_xdg_dir);
    g_clear_pointer(&test_xdg_dir, g_free);
}

static void setup_test_isolation(void)
{
    if (g_getenv("SILKTEX_TEST_XDG_DIR")) return; /* child of g_test_trap_subprocess */

    test_xdg_dir = g_dir_make_tmp("silktex-test-XXXXXX", NULL);
    g_assert_nonnull(test_xdg_dir);
    g_setenv("XDG_CONFIG_HOME", test_xdg_dir, TRUE);
    g_setenv("XDG_CACHE_HOME", test_xdg_dir, TRUE);
    g_setenv("SILKTEX_TEST_XDG_DIR", test_xdg_dir, TRUE);
    atexit(remove_test_xdg_dir);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    setup_test_isolation();
    g_test_init(&argc, &argv, NULL);

    /* utils.c */
    g_test_add_func("/utils/in_debug_mode/default",          test_in_debug_mode_default);
    g_test_add_func("/utils/in_debug_mode/enabled",          test_in_debug_mode_enabled);
    g_test_add_func("/utils/slog/does_not_crash",            test_slog_does_not_crash);
    g_test_add_func("/utils/path_exists/true",               test_utils_path_exists_true);
    g_test_add_func("/utils/path_exists/false",              test_utils_path_exists_false);
    g_test_add_func("/utils/path_exists/null",               test_utils_path_exists_null);
    g_test_add_func("/utils/copy_file/ok",                   test_utils_copy_file);
    g_test_add_func("/utils/copy_file/missing_src",          test_utils_copy_file_missing_src);
    g_test_add_func("/utils/subinstr/match",                 test_utils_subinstr_match);
    g_test_add_func("/utils/subinstr/no_match",              test_utils_subinstr_no_match);
    g_test_add_func("/utils/subinstr/case_insensitive",      test_utils_subinstr_case_insensitive);
    g_test_add_func("/utils/subinstr/case_sensitive_fail",   test_utils_subinstr_case_sensitive_fail);
    g_test_add_func("/utils/subinstr/null",                  test_utils_subinstr_null);
    g_test_add_func("/utils/subinstr/empty_needle",          test_utils_subinstr_empty_needle);
    g_test_add_func("/utils/subinstr/exact_match",           test_utils_subinstr_exact_match);
    g_test_add_func("/utils/subinstr/empty_haystack",        test_utils_subinstr_empty_haystack);
    g_test_add_func("/utils/subinstr/single_char",           test_utils_subinstr_single_char);
    g_test_add_func("/utils/g_substr/basic",                 test_g_substr_basic);
    g_test_add_func("/utils/g_substr/middle",                test_g_substr_middle);
    g_test_add_func("/utils/g_substr/empty_range",           test_g_substr_empty_range);
    g_test_add_func("/utils/g_substr/full_string",           test_g_substr_full_string);
    g_test_add_func("/utils/slist/find_exact",               test_slist_find_exact);
    g_test_add_func("/utils/slist/find_prefix",              test_slist_find_prefix);
    g_test_add_func("/utils/slist/find_create",              test_slist_find_create);
    g_test_add_func("/utils/slist/find_null_list",           test_slist_find_null_list);
    g_test_add_func("/utils/slist/append_and_find",          test_slist_append_and_find);
    g_test_add_func("/utils/slist/remove_head",              test_slist_remove_head);
    g_test_add_func("/utils/slist/remove_tail",              test_slist_remove_tail);
    g_test_add_func("/utils/slist/remove_single_node",       test_slist_remove_single_node);
    g_test_add_func("/utils/slist/remove_null_list",         test_slist_remove_null_list);

    /* latex.c — generate_table */
    g_test_add_func("/latex/table/basic",                    test_generate_table_basic);
    g_test_add_func("/latex/table/outer_borders",            test_generate_table_outer_borders);
    g_test_add_func("/latex/table/all_borders",              test_generate_table_all_borders);
    g_test_add_func("/latex/table/clamps_rows",              test_generate_table_clamps_rows);
    g_test_add_func("/latex/table/clamps_alignment",         test_generate_table_clamps_alignment);

    /* latex.c — generate_matrix */
    g_test_add_func("/latex/matrix/pmatrix",                 test_generate_matrix_pmatrix);
    g_test_add_func("/latex/matrix/bmatrix",                 test_generate_matrix_bmatrix);
    g_test_add_func("/latex/matrix/all_brackets",            test_generate_matrix_all_brackets);
    g_test_add_func("/latex/matrix/clamps_bracket",          test_generate_matrix_clamps_bracket);

    /* latex.c — generate_image */
    g_test_add_func("/latex/image/basic",                    test_generate_image_basic);
    g_test_add_func("/latex/image/zero_scale",               test_generate_image_zero_scale);
    g_test_add_func("/latex/image/null_args",                test_generate_image_null_args);
    g_test_add_func("/latex/image/half_scale",               test_generate_image_half_scale);

    /* git.c */
    g_test_add_func("/git/is_available",                     test_git_is_available);
    g_test_add_func("/git/status_load/non_repo",             test_git_status_load_non_repo);
    g_test_add_func("/git/status_load/real_repo",            test_git_status_load_real_repo);

    /* snippets.c */
    g_test_add_func("/snippets/new",                         test_snippets_new);
    g_test_add_func("/snippets/get_filename",                test_snippets_get_filename);
    g_test_add_func("/snippets/reload",                      test_snippets_reload);
    g_test_add_func("/snippets/reset_to_default",            test_snippets_reset_to_default);
    g_test_add_func("/snippets/set_modifiers",               test_snippets_set_modifiers);

    /* configfile.c */
    g_test_add_func("/config/string_roundtrip",              test_config_string_roundtrip);
    g_test_add_func("/config/boolean_roundtrip",             test_config_boolean_roundtrip);
    g_test_add_func("/config/integer_roundtrip",             test_config_integer_roundtrip);
    g_test_add_func("/config/missing_key_defaults",          test_config_missing_key_defaults);
    g_test_add_func("/config/overwrite",                     test_config_overwrite);
    g_test_add_func("/config/isolation_across_groups",       test_config_isolation_across_groups);
    g_test_add_func("/config/many_keys",                     test_config_many_keys);

    /* memory / lifecycle */
    g_test_add_func("/memory/snippets_lifecycle_stress",     test_snippets_lifecycle_stress);
    g_test_add_func("/memory/git_status_free_null",          test_git_status_free_null);

    /* hostile input */
    g_test_add_func("/isolation/uses_private_xdg_dir",       test_isolation_uses_private_xdg_dir);
    g_test_add_func("/utils/g_substr/nul_terminated",        test_g_substr_nul_terminated);
    g_test_add_func("/utils/g_substr/end_beyond_length",     test_g_substr_end_beyond_length);
    g_test_add_func("/utils/g_substr/start_gt_end",          test_g_substr_start_gt_end);
    g_test_add_func("/utils/g_substr/negative_end",          test_g_substr_negative_end);
    g_test_add_func("/utils/g_substr/negative_start",        test_g_substr_negative_start);
    g_test_add_func("/utils/slist/find_null_term_exact",     test_slist_find_null_term_exact);
    g_test_add_func("/utils/slist/find_null_term_prefix",    test_slist_find_null_term_prefix);
    g_test_add_func("/utils/slist/find_null_first_exact",    test_slist_find_null_first_exact);
    g_test_add_func("/utils/slist/find_null_first_prefix",   test_slist_find_null_first_prefix);
    g_test_add_func("/utils/slist/find_empty_prefix",        test_slist_find_empty_prefix_matches_head);
    g_test_add_func("/utils/slist/append_null_head",         test_slist_append_null_head);
    g_test_add_func("/utils/slist/append_null_node",         test_slist_append_null_node);
    g_test_add_func("/utils/slist/remove_null_node",         test_slist_remove_null_node);
    g_test_add_func("/utils/slist/remove_foreign_node",      test_slist_remove_foreign_node);
    g_test_add_func("/latex/table/negative_counts",          test_generate_table_negative_counts);
    g_test_add_func("/latex/table/large",                    test_generate_table_large);
    g_test_add_func("/latex/matrix/negative_counts",         test_generate_matrix_negative_counts);
    g_test_add_func("/latex/matrix/large",                   test_generate_matrix_large);
    g_test_add_func("/latex/image/format_chars",             test_generate_image_format_chars);
    g_test_add_func("/latex/image/negative_scale",           test_generate_image_negative_scale);
    g_test_add_func("/latex/image/huge_scale",               test_generate_image_huge_scale);
    g_test_add_func("/latex/image/empty_strings",            test_generate_image_empty_strings);
    g_test_add_func("/snippets/json/garbage",                test_snippets_json_garbage);
    g_test_add_func("/snippets/json/empty_file",             test_snippets_json_empty_file);
    g_test_add_func("/snippets/json/root_not_object",        test_snippets_json_root_not_object);
    g_test_add_func("/snippets/json/non_utf8",               test_snippets_json_non_utf8);
    g_test_add_func("/snippets/json/edge_bodies",            test_snippets_json_edge_bodies);
    g_test_add_func("/snippets/json/entry_not_object",       test_snippets_json_entry_not_object);
    g_test_add_func("/snippets/json/description_not_string", test_snippets_json_description_not_string);
    g_test_add_func("/snippets/json/body_array_non_string",  test_snippets_json_body_array_non_string);
    g_test_add_func("/config/garbage_ini",                   test_config_garbage_ini);
    g_test_add_func("/config/truncated_ini",                 test_config_truncated_ini);
    g_test_add_func("/config/integer_extremes",              test_config_integer_extremes);
    g_test_add_func("/config/integer_roundtrip_extremes",    test_config_integer_roundtrip_extremes);
    g_test_add_func("/config/string_specials_roundtrip",     test_config_string_specials_roundtrip);
    g_test_add_func("/config/missing_group",                 test_config_missing_group);

    /* collab.c — operational transform */
    g_test_add_func("/collab/transform/identity_no_local_op",       test_collab_tf_identity_no_local_op);
    g_test_add_func("/collab/transform/identity_zero_len_local",    test_collab_tf_identity_zero_len_local_at_same_offset);
    g_test_add_func("/collab/transform/null_pointers",              test_collab_tf_null_pointers_tolerated);
    g_test_add_func("/collab/transform/insert_before",              test_collab_tf_insert_before_shifts_retain);
    g_test_add_func("/collab/transform/insert_after",               test_collab_tf_insert_after_is_noop);
    g_test_add_func("/collab/transform/insert_at_remote_start",     test_collab_tf_insert_at_remote_start_shifts);
    g_test_add_func("/collab/transform/insert_at_remote_end",       test_collab_tf_insert_at_remote_end_is_noop);
    g_test_add_func("/collab/transform/insert_inside_remote_del",   test_collab_tf_insert_inside_remote_delete_grows_it);
    g_test_add_func("/collab/transform/insert_before_zero_width",   test_collab_tf_insert_before_zero_width_remote);
    g_test_add_func("/collab/transform/delete_before",              test_collab_tf_delete_before_shifts_retain_back);
    g_test_add_func("/collab/transform/delete_ends_at_remote",      test_collab_tf_delete_ending_exactly_at_remote_start);
    g_test_add_func("/collab/transform/delete_after",               test_collab_tf_delete_after_is_noop);
    g_test_add_func("/collab/transform/delete_overlaps_head",       test_collab_tf_delete_overlaps_remote_head);
    g_test_add_func("/collab/transform/delete_overlaps_tail",       test_collab_tf_delete_overlaps_remote_tail);
    g_test_add_func("/collab/transform/delete_contains_remote",     test_collab_tf_delete_contains_remote_range);
    g_test_add_func("/collab/transform/delete_inside_remote",       test_collab_tf_delete_inside_remote_range);
    g_test_add_func("/collab/transform/identical_deletes",          test_collab_tf_identical_deletes_cancel);
    g_test_add_func("/collab/transform/remote_ins_inside_local_del",test_collab_tf_remote_insert_inside_local_delete_dropped);
    g_test_add_func("/collab/transform/remote_ins_at_del_start",    test_collab_tf_remote_insert_at_local_delete_start_survives);
    g_test_add_func("/collab/transform/remote_ins_at_del_end",      test_collab_tf_remote_insert_at_local_delete_end_survives);
    g_test_add_func("/collab/transform/remote_ins_vs_local_ins",    test_collab_tf_remote_insert_survives_local_insert);
    g_test_add_func("/collab/transform/never_negative",             test_collab_tf_never_returns_negative);
    g_test_add_func("/collab/transform/negative_local_is_zero",     test_collab_tf_negative_local_treated_as_zero);
    g_test_add_func("/collab/transform/huge_values_clamped",        test_collab_tf_huge_values_stay_in_range);
    g_test_add_func("/collab/transform/random_convergence",         test_collab_tf_random_convergence);

    return g_test_run();
}

/*
 * ═══════════════════════════════════════════════════════════════════════
 *  INTERACTIVE TEST CHECKLIST
 *  (functions that require a live GTK display — exercise manually)
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  application.c
 *    silktex_application_new()            → launch the app
 *
 *  window.c
 *    silktex_window_new()                 → app startup
 *    silktex_window_new_tab()             → Ctrl+T
 *    silktex_window_open_file()           → Ctrl+O, open test.tex
 *    silktex_window_show_toast()          → save a file
 *    silktex_window_update_window_title() → switch tabs
 *    silktex_window_update_tab_title()    → edit a file
 *    silktex_window_update_page_label()   → page forward/back in preview
 *    silktex_window_update_log_panel()    → trigger a compile error
 *    silktex_window_focus_active_editor() → switch tabs
 *    silktex_window_restart_compile_timer()→ edit with auto-compile on
 *    silktex_window_restart_autosave_timer()→ edit with autosave on
 *    silktex_window_apply_editor_paned_half_split() → drag the pane divider
 *    silktex_window_apply_theme_to_editor() → toggle dark/light mode
 *    silktex_window_apply_theme_to_all_editors() → toggle dark/light mode
 *    silktex_window_apply_preview_theme() → toggle inverted preview
 *    silktex_window_install_chrome_css()  → app startup
 *    silktex_window_install_primary_menu()→ app startup
 *    silktex_window_register_menu_actions()→ app startup
 *
 *  editor.c
 *    silktex_editor_new()                 → open a file
 *    silktex_editor_load_file()           → Ctrl+O
 *    silktex_editor_save_file()           → Ctrl+S
 *    silktex_editor_get/set_text()        → type in the editor
 *    silktex_editor_get/set_modified()    → edit/save a file
 *    silktex_editor_undo/redo()           → Ctrl+Z / Ctrl+Y
 *    silktex_editor_can_undo/redo()       → undo/redo button state
 *    silktex_editor_set_font()            → change font in prefs
 *    silktex_editor_set_style_scheme()    → change scheme in prefs
 *    silktex_editor_apply_settings()      → save prefs
 *    silktex_editor_apply_textstyle()     → Insert → Bold/Italic/…
 *    silktex_editor_insert_package()      → Insert → Package
 *    silktex_editor_goto_line()           → SyncTeX inverse click
 *    silktex_editor_scroll_to_line()      → SyncTeX inverse click
 *    silktex_editor_scroll_to_cursor()    → compile → jump to error
 *    silktex_editor_search()              → Ctrl+F, type a query
 *    silktex_editor_search_next()         → F3 / Enter in search bar
 *    silktex_editor_replace()             → Ctrl+H, replace one
 *    silktex_editor_replace_all()         → Ctrl+H, replace all
 *    silktex_editor_zoom_in/out/reset()   → Ctrl+=/−/0
 *    silktex_editor_get_cursor_line()     → forward SyncTeX
 *    silktex_editor_get_basename()        → title bar
 *    silktex_editor_get_source_dir()      → compile
 *    silktex_editor_get_filename()        → compile/save
 *    silktex_editor_get_pdffile()         → preview load
 *    silktex_editor_get_workfile()        → compiler temp file
 *    silktex_editor_update_workfile()     → compile trigger
 *    silktex_editor_get_buffer()          → snippet expansion
 *    silktex_editor_get_view()            → snippet expansion
 *    silktex_editor_get/set_modified()    → save state
 *    silktex_editor_set_filename()        → Save As
 *
 *  compiler.c
 *    silktex_compiler_new()               → window init
 *    silktex_compiler_start()             → app startup
 *    silktex_compiler_stop()              → app shutdown
 *    silktex_compiler_request_compile()   → edit with auto-compile
 *    silktex_compiler_force_compile()     → Ctrl+B
 *    silktex_compiler_cancel()            → stop a long compile
 *    silktex_compiler_pause/resume()      → (internal)
 *    silktex_compiler_run_bibtex()        → Insert → Bibliography, compile
 *    silktex_compiler_run_makeindex()     → \index{} + compile
 *    silktex_compiler_is_running/compiling() → status bar
 *    silktex_compiler_apply_config()      → save prefs
 *    silktex_compiler_get_log()           → compile → view log panel
 *    silktex_compiler_get_error_lines()   → compile with errors
 *    silktex_compiler_set_typesetter()    → choose pdflatex/xelatex in prefs
 *    silktex_compiler_set_shell_escape()  → enable shell escape in prefs
 *    silktex_compiler_set_synctex()       → enable SyncTeX in prefs
 *    silktex_compiler_get_typesetter()    → prefs display
 *
 *  preview.c
 *    silktex_preview_new()                → window init
 *    silktex_preview_load_file()          → compile succeeds
 *    silktex_preview_refresh()            → recompile
 *    silktex_preview_clear()              → close file
 *    silktex_preview_set/get_page()       → page nav buttons
 *    silktex_preview_next/prev_page()     → PgDn/PgUp
 *    silktex_preview_get_n_pages()        → page label
 *    silktex_preview_set/get_zoom()       → zoom slider
 *    silktex_preview_zoom_in/out()        → Ctrl+=/-
 *    silktex_preview_zoom_fit_page()      → fit-page button
 *    silktex_preview_zoom_fit_width()     → fit-width button
 *    silktex_preview_toggle_zoom_fit_page/width() → toggle buttons
 *    silktex_preview_set/get_inverted()   → invert toggle
 *    silktex_preview_set_layout()         → single/continuous toggle
 *    silktex_preview_get_layout()         → layout state
 *    silktex_preview_get_zoom_mode()      → zoom mode state
 *    silktex_preview_scroll_to_position() → SyncTeX forward click
 *
 *  synctex.c
 *    silktex_synctex_forward()            → Ctrl+click in editor
 *    silktex_synctex_inverse()            → Ctrl+click in PDF preview
 *
 *  searchbar.c
 *    silktex_searchbar_new()              → window init
 *    silktex_searchbar_set_editor()       → tab switch
 *    silktex_searchbar_open()             → Ctrl+F / Ctrl+H
 *    silktex_searchbar_close()            → Escape
 *    silktex_searchbar_is_open()          → keyboard handler
 *
 *  structure.c
 *    silktex_structure_new()              → window init
 *    silktex_structure_set_editor()       → tab switch
 *    silktex_structure_refresh()          → edit document structure
 *
 *  prefs.c
 *    silktex_prefs_new()                  → open Preferences
 *    silktex_prefs_set_apply_callback()   → window init
 *    silktex_prefs_set_snippets()         → window init
 *    silktex_prefs_present()              → Ctrl+,
 *    import_gummi_snippets()              → Preferences → Import… → .cfg
 *    import_texstudio_macro()             → Preferences → Import… → .txsMacro
 *
 *  cmdpalette.c
 *    silktex_cmd_palette_show()           → Ctrl+Shift+P
 *
 *  collab.c
 *    silktex_collab_setup_window()        → window init
 *    silktex_collab_connect_editor()      → tab switch
 *    do_create_session()                  → Collaboration → Start Session
 *    do_join_session()                    → Collaboration → Join Session
 *    do_leave_session()                   → Collaboration → Leave Session
 *
 *  latex.c — dialog wrappers (display required)
 *    silktex_latex_insert_image_dialog()  → Insert → Image
 *    silktex_latex_insert_table_dialog()  → Insert → Table
 *    silktex_latex_insert_matrix_dialog() → Insert → Matrix
 *    silktex_latex_insert_biblio_dialog() → Insert → Bibliography
 *    silktex_latex_insert_at_cursor()     → any insert action
 *    silktex_latex_insert_structure()     → Insert → Section/…
 *    silktex_latex_insert_environment()   → Insert → Environment
 *
 *  window-git.c
 *    silktex_window_git_register_actions()→ window init
 *    silktex_window_git_refresh_state()   → open git-tracked file
 *    silktex_window_git_update_actions()  → file status change
 *    action_git_status()                  → Git → Status
 *    action_git_commit()                  → Git → Commit
 *    action_git_pull()                    → Git → Pull
 *    action_git_push()                    → Git → Push
 *    silktex_git_stage_file()             → stage a file in git dialog
 *    silktex_git_unstage_file()           → unstage a file in git dialog
 *    silktex_git_pull()                   → Git → Pull
 *    silktex_git_push()                   → Git → Push
 *    silktex_git_commit()                 → Git → Commit
 *
 *  style-schemes.c
 *    silktex_init_style_scheme_paths()    → prefs open
 *    silktex_resolved_style_scheme_id()   → editor init
 * ═══════════════════════════════════════════════════════════════════════ */
