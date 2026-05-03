/*
 * SilkTex — speed benchmarks
 *
 * Each benchmark runs a tight loop, measures wall-clock time with
 * g_test_timer_start/elapsed, and reports throughput via
 * g_test_maximized_result (higher = better).
 *
 * Run:
 *   meson test -C build-gtk4 --benchmark --verbose
 */

#include <glib.h>
#include <string.h>
#include "../src/utils.h"
#include "../src/latex.h"
#include "../src/snippets.h"
#include "../src/configfile.h"

#define ITERS 100000
#define ITERS_HEAVY 1000

/* Report throughput both to GLib perf infrastructure and as a visible message */
#define PERF_REPORT(qty, ...) \
    do { \
        double _qty = (qty); \
        g_test_maximized_result(_qty, __VA_ARGS__); \
        g_autofree char *_label = g_strdup_printf(__VA_ARGS__); \
        g_test_message("PERF  %14.0f  %s", _qty, _label); \
    } while (0)

/* ═══════════════════════════════════════════════════════════════════════
 *  latex.c generators
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_generate_table_small(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *t = silktex_latex_generate_table(3, 4, 0, 1);
        g_free(t);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "table-gen/sec (3×4, no borders)");
}

static void bench_generate_table_large(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS_HEAVY; i++) {
        char *t = silktex_latex_generate_table(50, 50, 2, 1);
        g_free(t);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS_HEAVY / elapsed, "table-gen/sec (50×50, all borders)");
}

static void bench_generate_matrix_small(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *m = silktex_latex_generate_matrix(1, 3, 3);
        g_free(m);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "matrix-gen/sec (3×3 pmatrix)");
}

static void bench_generate_matrix_large(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS_HEAVY; i++) {
        char *m = silktex_latex_generate_matrix(2, 20, 20);
        g_free(m);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS_HEAVY / elapsed, "matrix-gen/sec (20×20 bmatrix)");
}

static void bench_generate_image(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *img = silktex_latex_generate_image("figures/plot.pdf", "A caption", "fig:plot", 0.75);
        g_free(img);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "image-gen/sec");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  utils.c
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_subinstr_hit(void)
{
    /* 200-char haystack, needle near the end */
    const char *hay =
        "\\begin{document}\\section{Introduction}\\label{sec:intro}"
        "This is a long paragraph of LaTeX source that the editor has to search "
        "through repeatedly as the user types. needle is somewhere in here.\\end{document}";

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_subinstr("needle", hay, FALSE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-sensitive)");
}

static void bench_subinstr_miss(void)
{
    const char *hay =
        "\\begin{document}\\section{Introduction}\\label{sec:intro}"
        "This is a long paragraph of LaTeX source that the editor has to search "
        "through repeatedly as the user types.\\end{document}";

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_subinstr("zzz_not_here", hay, FALSE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "subinstr/sec (miss, case-sensitive)");
}

static void bench_subinstr_icase(void)
{
    const char *hay =
        "\\begin{document}\\section{Introduction}\\label{sec:intro}"
        "This is a long paragraph of LaTeX source. NEEDLE is here.\\end{document}";

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_subinstr("needle", hay, TRUE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-insensitive)");
}

static void bench_subinstr_long_haystack(void)
{
    /* 10 KB of LaTeX — realistic document size for incremental search */
    GString *buf = g_string_sized_new(10240);
    g_string_append(buf, "\\begin{document}\n");
    for (int i = 0; i < 200; i++)
        g_string_append_printf(buf, "\\section{Section %d} This is paragraph %d of the document.\n", i, i);
    g_string_append(buf, "\\textbf{TARGET} \\end{document}\n");
    const char *hay = buf->str;

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_subinstr("TARGET", hay, FALSE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-sensitive, 10 KB haystack)");

    g_string_free(buf, TRUE);
}

static void bench_subinstr_long_haystack_icase(void)
{
    GString *buf = g_string_sized_new(10240);
    g_string_append(buf, "\\begin{document}\n");
    for (int i = 0; i < 200; i++)
        g_string_append_printf(buf, "\\section{Section %d} This is paragraph %d.\n", i, i);
    g_string_append(buf, "\\textbf{target} \\end{document}\n");
    const char *hay = buf->str;

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_subinstr("TARGET", hay, TRUE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-insensitive, 10 KB haystack)");

    g_string_free(buf, TRUE);
}

static void bench_g_substr(void)
{
    char *src = "\\begin{document}\\section{Introduction}\\end{document}";

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *s = g_substr(src, 7, 15);
        g_free(s);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "g_substr/sec");
}

static void bench_utils_path_exists_true(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_path_exists("/tmp");
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "path_exists/sec (hit)");
}

static void bench_utils_path_exists_false(void)
{
    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        gboolean r = utils_path_exists("/silktex_no_such_path_xyz");
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "path_exists/sec (miss)");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  slist
 * ═══════════════════════════════════════════════════════════════════════ */

#define LIST_LEN 256

static slist *build_slist(int n)
{
    slist *head = NULL, *tail = NULL;
    for (int i = 0; i < n; i++) {
        slist *node = g_new0(slist, 1);
        node->first  = g_strdup_printf("entry_%04d", i);
        node->second = g_strdup("");
        if (!head) { head = tail = node; }
        else       { tail->next = node; tail = node; }
    }
    return head;
}

static void free_slist_nodes(slist *head)
{
    while (head) {
        slist *next = head->next;
        g_free(head->first);
        g_free(head->second);
        g_free(head);
        head = next;
    }
}

static void bench_slist_find_head(void)
{
    slist *list = build_slist(LIST_LEN);

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        slist *r = slist_find(list, "entry_0000", FALSE, FALSE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "slist_find/sec (head, list len=%d)", LIST_LEN);

    free_slist_nodes(list);
}

static void bench_slist_find_tail(void)
{
    slist *list = build_slist(LIST_LEN);
    g_autofree char *tail_key = g_strdup_printf("entry_%04d", LIST_LEN - 1);

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        slist *r = slist_find(list, tail_key, FALSE, FALSE);
        (void)r;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "slist_find/sec (tail, list len=%d)", LIST_LEN);

    free_slist_nodes(list);
}

static void bench_slist_build_free(void)
{
    /* Allocation churn: build and fully free a 1 000-node list repeatedly.
     * Exercises g_new0/g_free and g_strdup under G_SLICE=always-malloc. */
    g_test_timer_start();
    for (int i = 0; i < 500; i++) {
        slist *list = build_slist(1000);
        free_slist_nodes(list);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(500 / elapsed, "slist build+free 1000-node list /sec");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  snippets.c
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_snippets_new_unref(void)
{
    g_test_timer_start();
    for (int i = 0; i < 1000; i++) {
        SilktexSnippets *s = silktex_snippets_new();
        g_object_unref(s);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(1000 / elapsed, "snippets new+unref/sec");
}

static void bench_snippets_reload(void)
{
    SilktexSnippets *s = silktex_snippets_new();

    g_test_timer_start();
    for (int i = 0; i < 500; i++) {
        silktex_snippets_reload(s);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(500 / elapsed, "snippets_reload/sec");

    g_object_unref(s);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  configfile.c
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_config_get_string(void)
{
    config_init();
    config_set_string("Bench", "key", "hello");

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        const char *v = config_get_string("Bench", "key");
        (void)v;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "config_get_string/sec");
}

static void bench_config_set_string(void)
{
    config_init();

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        config_set_string("Bench", "key", "value");
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "config_set_string/sec (in-memory)");
}

static void bench_config_many_unique_keys(void)
{
    /* Populate 100 keys across 10 groups, then read them all back.
     * Reflects the real startup pattern: many distinct config keys read once. */
    config_init();
    for (int g = 0; g < 10; g++) {
        for (int k = 0; k < 10; k++) {
            g_autofree char *group = g_strdup_printf("BenchGroup%d", g);
            g_autofree char *key   = g_strdup_printf("key_%d", k);
            config_set_integer(group, key, g * 10 + k);
        }
    }

    g_test_timer_start();
    for (int i = 0; i < 5000; i++) {
        int g = i % 10, k = (i / 10) % 10;
        g_autofree char *group = g_strdup_printf("BenchGroup%d", g);
        g_autofree char *key   = g_strdup_printf("key_%d", k);
        int v = config_get_integer(group, key);
        (void)v;
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(5000 / elapsed, "config_get_integer/sec (100 unique keys, 10 groups)");
}

static void bench_g_substr_large(void)
{
    /* 1 KB string — exercises the allocator for larger substrings */
    GString *buf = g_string_sized_new(1024);
    for (int i = 0; i < 64; i++)
        g_string_append(buf, "abcdefghijklmnop");
    char *src = buf->str;
    int   len = buf->len;

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *s = g_substr(src, 0, len);
        g_free(s);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "g_substr/sec (1 KB string)");

    g_string_free(buf, TRUE);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    /* latex generators */
    g_test_add_func("/bench/latex/table/small",          bench_generate_table_small);
    g_test_add_func("/bench/latex/table/large",          bench_generate_table_large);
    g_test_add_func("/bench/latex/matrix/small",         bench_generate_matrix_small);
    g_test_add_func("/bench/latex/matrix/large",         bench_generate_matrix_large);
    g_test_add_func("/bench/latex/image",                bench_generate_image);

    /* utils */
    g_test_add_func("/bench/utils/subinstr/hit",                bench_subinstr_hit);
    g_test_add_func("/bench/utils/subinstr/miss",               bench_subinstr_miss);
    g_test_add_func("/bench/utils/subinstr/icase",              bench_subinstr_icase);
    g_test_add_func("/bench/utils/subinstr/long_haystack",      bench_subinstr_long_haystack);
    g_test_add_func("/bench/utils/subinstr/long_haystack_icase",bench_subinstr_long_haystack_icase);
    g_test_add_func("/bench/utils/g_substr",                    bench_g_substr);
    g_test_add_func("/bench/utils/g_substr/large",              bench_g_substr_large);
    g_test_add_func("/bench/utils/path_exists/hit",      bench_utils_path_exists_true);
    g_test_add_func("/bench/utils/path_exists/miss",     bench_utils_path_exists_false);

    /* slist */
    g_test_add_func("/bench/slist/find_head",            bench_slist_find_head);
    g_test_add_func("/bench/slist/find_tail",            bench_slist_find_tail);
    g_test_add_func("/bench/slist/build_free",           bench_slist_build_free);

    /* snippets */
    g_test_add_func("/bench/snippets/new_unref",         bench_snippets_new_unref);
    g_test_add_func("/bench/snippets/reload",            bench_snippets_reload);

    /* config */
    g_test_add_func("/bench/config/get_string",          bench_config_get_string);
    g_test_add_func("/bench/config/set_string",          bench_config_set_string);
    g_test_add_func("/bench/config/many_unique_keys",    bench_config_many_unique_keys);

    return g_test_run();
}
