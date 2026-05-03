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
#include <string.h>

/* ── headers for testable modules ─────────────────────────────────────── */
#include "../src/utils.h"
#include "../src/latex.h"
#include "../src/git.h"
#include "../src/snippets.h"
#include "../src/configfile.h"

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
 *  main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
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
