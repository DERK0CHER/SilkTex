/*
 * SilkTex — speed benchmarks
 *
 * Each benchmark:
 *   1. Warms up (WARMUP macro) so instruction/branch caches are hot.
 *   2. Accumulates results into _sink so the compiler cannot eliminate
 *      timed calls as dead code.
 *   3. Reports ops/sec via PERF_REPORT (higher = better).
 *      Search benchmarks additionally report MB/s.
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

/* ─── tuning ────────────────────────────────────────────────────────────── */
#define ITERS       100000
#define ITERS_HEAVY   1000

/* ─── anti-optimisation sink ────────────────────────────────────────────── */
/* XOR results into a volatile so the compiler cannot prove timed calls are
 * dead code and eliminate them.  Printed once at the end. */
static volatile unsigned _sink = 0;
#define SINK(x) (_sink ^= (unsigned)(uintptr_t)(x))

/* ─── warmup ─────────────────────────────────────────────────────────────── */
/* Run 10 % of the iteration count before starting the timer to fill
 * instruction caches and warm branch predictors. */
#define WARMUP(n, ...) \
    do { for (int _w = 0; _w < (n) / 10; _w++) { __VA_ARGS__ } } while (0)

/* ─── reporting ──────────────────────────────────────────────────────────── */
/* Primary metric: feeds g_test_maximized_result (call at most once per test)
 * and prints to the log. */
#define PERF_REPORT(qty, ...) \
    do { \
        double _qty = (qty); \
        g_test_maximized_result(_qty, __VA_ARGS__); \
        g_autofree char *_label = g_strdup_printf(__VA_ARGS__); \
        g_test_message("PERF  %14.0f  %s", _qty, _label); \
    } while (0)

/* Secondary metric: log only — does NOT call g_test_maximized_result so it
 * can be used any number of times alongside one PERF_REPORT per test. */
#define PERF_MSG(qty, ...) \
    do { \
        double _qty = (qty); \
        g_autofree char *_label = g_strdup_printf(__VA_ARGS__); \
        g_test_message("INFO  %14.0f  %s", _qty, _label); \
    } while (0)

/* ═══════════════════════════════════════════════════════════════════════
 *  latex.c generators
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_generate_table_small(void)
{
    WARMUP(ITERS, { char *t = silktex_latex_generate_table(3, 4, 0, 1); SINK(t); g_free(t); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *t = silktex_latex_generate_table(3, 4, 0, 1);
        SINK(t);
        g_free(t);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "table-gen/sec (3×4, no borders)");
}

static void bench_generate_table_large(void)
{
    WARMUP(ITERS_HEAVY, { char *t = silktex_latex_generate_table(50, 50, 2, 1); SINK(t); g_free(t); });

    g_test_timer_start();
    for (int i = 0; i < ITERS_HEAVY; i++) {
        char *t = silktex_latex_generate_table(50, 50, 2, 1);
        SINK(t);
        g_free(t);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS_HEAVY / elapsed, "table-gen/sec (50×50, all borders)");
}

static void bench_generate_matrix_small(void)
{
    WARMUP(ITERS, { char *m = silktex_latex_generate_matrix(1, 3, 3); SINK(m); g_free(m); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *m = silktex_latex_generate_matrix(1, 3, 3);
        SINK(m);
        g_free(m);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "matrix-gen/sec (3×3 pmatrix)");
}

static void bench_generate_matrix_large(void)
{
    WARMUP(ITERS_HEAVY, { char *m = silktex_latex_generate_matrix(2, 20, 20); SINK(m); g_free(m); });

    g_test_timer_start();
    for (int i = 0; i < ITERS_HEAVY; i++) {
        char *m = silktex_latex_generate_matrix(2, 20, 20);
        SINK(m);
        g_free(m);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS_HEAVY / elapsed, "matrix-gen/sec (20×20 bmatrix)");
}

static void bench_generate_image(void)
{
    WARMUP(ITERS, {
        char *img = silktex_latex_generate_image("figures/plot.pdf", "A caption", "fig:plot", 0.75);
        SINK(img); g_free(img);
    });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *img = silktex_latex_generate_image("figures/plot.pdf", "A caption", "fig:plot", 0.75);
        SINK(img);
        g_free(img);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "image-gen/sec");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  utils.c — subinstr
 *
 *  For search functions we report both ops/sec and MB/s so scalability
 *  across document sizes is immediately visible.
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_subinstr_hit(void)
{
    /* ~200-char haystack; needle near the end — measures forward-scan cost */
    const char *hay =
        "\\begin{document}\\section{Introduction}\\label{sec:intro}"
        "This is a long paragraph of LaTeX source that the editor has to search "
        "through repeatedly as the user types. needle is somewhere in here.\\end{document}";
    size_t hay_bytes = strlen(hay);

    WARMUP(ITERS, { SINK(utils_subinstr("needle", hay, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_subinstr("needle", hay, FALSE));
    double elapsed = g_test_timer_elapsed();

    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-sensitive, 200 B)");
    PERF_MSG((double)ITERS * hay_bytes / elapsed / (1 << 20),
                "subinstr MB/s (hit, case-sensitive, 200 B)");
}

static void bench_subinstr_miss(void)
{
    /* Needle absent — strstr scans to end every time (worst case for short haystack) */
    const char *hay =
        "\\begin{document}\\section{Introduction}\\label{sec:intro}"
        "This is a long paragraph of LaTeX source that the editor has to search "
        "through repeatedly as the user types.\\end{document}";
    size_t hay_bytes = strlen(hay);

    WARMUP(ITERS, { SINK(utils_subinstr("zzz_not_here", hay, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_subinstr("zzz_not_here", hay, FALSE));
    double elapsed = g_test_timer_elapsed();

    PERF_REPORT(ITERS / elapsed, "subinstr/sec (miss, case-sensitive, 200 B)");
    PERF_MSG((double)ITERS * hay_bytes / elapsed / (1 << 20),
                "subinstr MB/s (miss, case-sensitive, 200 B)");
}

static void bench_subinstr_icase(void)
{
    const char *hay =
        "\\begin{document}\\section{Introduction}\\label{sec:intro}"
        "This is a long paragraph of LaTeX source. NEEDLE is here.\\end{document}";
    size_t hay_bytes = strlen(hay);

    WARMUP(ITERS, { SINK(utils_subinstr("needle", hay, TRUE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_subinstr("needle", hay, TRUE));
    double elapsed = g_test_timer_elapsed();

    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-insensitive, 200 B)");
    PERF_MSG((double)ITERS * hay_bytes / elapsed / (1 << 20),
                "subinstr MB/s (hit, case-insensitive, 200 B)");
}

static void bench_subinstr_long_haystack(void)
{
    /* 10 KB LaTeX — realistic document size for incremental search */
    GString *buf = g_string_sized_new(10240);
    g_string_append(buf, "\\begin{document}\n");
    for (int i = 0; i < 200; i++)
        g_string_append_printf(buf,
            "\\section{Section %d} This is paragraph %d of the document.\n", i, i);
    g_string_append(buf, "\\textbf{TARGET} \\end{document}\n");
    const char *hay = buf->str;
    size_t hay_bytes = buf->len;

    WARMUP(ITERS, { SINK(utils_subinstr("TARGET", hay, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_subinstr("TARGET", hay, FALSE));
    double elapsed = g_test_timer_elapsed();

    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-sensitive, 10 KB)");
    PERF_MSG((double)ITERS * hay_bytes / elapsed / (1 << 20),
                "subinstr MB/s (hit, case-sensitive, 10 KB)");

    g_string_free(buf, TRUE);
}

static void bench_subinstr_long_haystack_icase(void)
{
    GString *buf = g_string_sized_new(10240);
    g_string_append(buf, "\\begin{document}\n");
    for (int i = 0; i < 200; i++)
        g_string_append_printf(buf,
            "\\section{Section %d} This is paragraph %d.\n", i, i);
    g_string_append(buf, "\\textbf{target} \\end{document}\n");
    const char *hay = buf->str;
    size_t hay_bytes = buf->len;

    WARMUP(ITERS, { SINK(utils_subinstr("TARGET", hay, TRUE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_subinstr("TARGET", hay, TRUE));
    double elapsed = g_test_timer_elapsed();

    PERF_REPORT(ITERS / elapsed, "subinstr/sec (hit, case-insensitive, 10 KB)");
    PERF_MSG((double)ITERS * hay_bytes / elapsed / (1 << 20),
                "subinstr MB/s (hit, case-insensitive, 10 KB)");

    g_string_free(buf, TRUE);
}

static void bench_subinstr_miss_full_scan(void)
{
    /* Needle absent in a 10 KB document — every call scans the whole buffer.
     * This is the worst case: the user typed a query that matches nothing. */
    GString *buf = g_string_sized_new(10240);
    g_string_append(buf, "\\begin{document}\n");
    for (int i = 0; i < 200; i++)
        g_string_append_printf(buf,
            "\\section{Section %d} ordinary LaTeX content here.\n", i);
    g_string_append(buf, "\\end{document}\n");
    const char *hay = buf->str;
    size_t hay_bytes = buf->len;

    WARMUP(ITERS, { SINK(utils_subinstr("zzznomatch", hay, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_subinstr("zzznomatch", hay, FALSE));
    double elapsed = g_test_timer_elapsed();

    PERF_REPORT(ITERS / elapsed, "subinstr/sec (miss, case-sensitive, 10 KB full scan)");
    PERF_MSG((double)ITERS * hay_bytes / elapsed / (1 << 20),
                "subinstr MB/s (miss, case-sensitive, 10 KB full scan)");

    g_string_free(buf, TRUE);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  utils.c — g_substr
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_g_substr(void)
{
    char *src = "\\begin{document}\\section{Introduction}\\end{document}";

    WARMUP(ITERS, { char *s = g_substr(src, 7, 15); SINK(s); g_free(s); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *s = g_substr(src, 7, 15);
        SINK(s);
        g_free(s);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "g_substr/sec (8 B)");
}

static void bench_g_substr_large(void)
{
    GString *buf = g_string_sized_new(1024);
    for (int i = 0; i < 64; i++)
        g_string_append(buf, "abcdefghijklmnop");
    char *src = buf->str;
    int   len = buf->len;

    WARMUP(ITERS, { char *s = g_substr(src, 0, len); SINK(s); g_free(s); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++) {
        char *s = g_substr(src, 0, len);
        SINK(s);
        g_free(s);
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "g_substr/sec (1 KB)");

    g_string_free(buf, TRUE);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  utils.c — path_exists
 * ═══════════════════════════════════════════════════════════════════════ */

static void bench_utils_path_exists_true(void)
{
    WARMUP(ITERS, { SINK(utils_path_exists("/tmp")); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_path_exists("/tmp"));
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "path_exists/sec (hit)");
}

static void bench_utils_path_exists_false(void)
{
    WARMUP(ITERS, { SINK(utils_path_exists("/silktex_no_such_path_xyz")); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(utils_path_exists("/silktex_no_such_path_xyz"));
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

    WARMUP(ITERS, { SINK(slist_find(list, "entry_0000", FALSE, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(slist_find(list, "entry_0000", FALSE, FALSE));
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "slist_find/sec (exact, head, len=%d)", LIST_LEN);

    free_slist_nodes(list);
}

static void bench_slist_find_tail(void)
{
    slist *list = build_slist(LIST_LEN);
    g_autofree char *tail_key = g_strdup_printf("entry_%04d", LIST_LEN - 1);

    WARMUP(ITERS, { SINK(slist_find(list, tail_key, FALSE, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(slist_find(list, tail_key, FALSE, FALSE));
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "slist_find/sec (exact, tail, len=%d)", LIST_LEN);

    free_slist_nodes(list);
}

static void bench_slist_find_prefix_mid(void)
{
    /* Prefix match against the middle of a 256-entry list.
     * This is the hot path in the snippet engine: the user typed a prefix
     * and silktex_snippets_handle_key scans for a matching trigger. */
    slist *list = build_slist(LIST_LEN);
    /* "entry_01" is a prefix that matches entry_0100..entry_0199 — forces
     * a scan through the first ~65% of the list before the first hit. */
    const char *prefix = "entry_01";

    WARMUP(ITERS, { SINK(slist_find(list, prefix, TRUE, FALSE)); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(slist_find(list, prefix, TRUE, FALSE));
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "slist_find/sec (prefix, mid-list, len=%d)", LIST_LEN);

    free_slist_nodes(list);
}

static void bench_slist_build_free(void)
{
    /* Allocation churn: build and fully free a 1 000-node list.
     * Baseline for any future arena/pool allocator optimisation. */
    WARMUP(50, { slist *l = build_slist(1000); free_slist_nodes(l); });

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
    WARMUP(100, { SilktexSnippets *s = silktex_snippets_new(); g_object_unref(s); });

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

    WARMUP(50, { silktex_snippets_reload(s); });

    g_test_timer_start();
    for (int i = 0; i < 500; i++)
        silktex_snippets_reload(s);
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

    WARMUP(ITERS, { SINK(config_get_string("Bench", "key")); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(config_get_string("Bench", "key"));
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "config_get_string/sec");
}

static void bench_config_set_string(void)
{
    config_init();

    WARMUP(ITERS, { config_set_string("Bench", "key", "value"); });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        config_set_string("Bench", "key", "value");
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "config_set_string/sec (in-memory)");
}

static void bench_config_many_unique_keys(void)
{
    /* Populate 100 keys across 10 groups, then read them all back.
     * Key strings are pre-built so the hot loop measures only GKeyFile
     * lookups, not string allocation. */
    config_init();

    char *groups[10], *keys[10];
    for (int i = 0; i < 10; i++) {
        groups[i] = g_strdup_printf("BenchGroup%d", i);
        keys[i]   = g_strdup_printf("key_%d", i);
    }
    for (int i = 0; i < 10; i++)
        for (int k = 0; k < 10; k++)
            config_set_integer(groups[i], keys[k], i * 10 + k);

    WARMUP(500, {
        int gi = 3, ki = 7;
        SINK(config_get_integer(groups[gi], keys[ki]));
    });

    g_test_timer_start();
    for (int i = 0; i < 5000; i++) {
        int gi = i % 10, ki = (i / 10) % 10;
        SINK(config_get_integer(groups[gi], keys[ki]));
    }
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(5000 / elapsed, "config_get_integer/sec (100 keys, 10 groups)");

    for (int i = 0; i < 10; i++) { g_free(groups[i]); g_free(keys[i]); }
}

static void bench_config_get_string_realistic(void)
{
    /* Round-robin over the actual config keys the app reads on every
     * apply-settings call.  Measures the cache-warm steady-state cost. */
    config_init();

    static const struct { const char *group; const char *key; } keys[] = {
        { "Editor",    "font"              },
        { "Editor",    "style_scheme"      },
        { "Editor",    "line_numbers"      },
        { "Editor",    "textwrapping"      },
        { "Editor",    "tabwidth"          },
        { "Compile",   "typesetter"        },
        { "Compile",   "auto_compile"      },
        { "Compile",   "synctex"           },
        { "Interface", "theme"             },
        { "Snippets",  "modifier1"         },
        { "Snippets",  "modifier2"         },
        { "File",      "autosaving"        },
    };
    int nkeys = (int)(sizeof keys / sizeof keys[0]);

    WARMUP(ITERS, {
        int j = 0 % nkeys;
        SINK(config_get_string(keys[j].group, keys[j].key));
    });

    g_test_timer_start();
    for (int i = 0; i < ITERS; i++)
        SINK(config_get_string(keys[i % nkeys].group, keys[i % nkeys].key));
    double elapsed = g_test_timer_elapsed();
    PERF_REPORT(ITERS / elapsed, "config_get_string/sec (12 real app keys)");
}

/* ─── drain sink so its value is visible ──────────────────────────────── */
static void bench_drain_sink(void)
{
    g_test_message("sink=0x%08x (non-zero means results were not optimised out)", _sink);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  main
 * ═══════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    /* latex generators */
    g_test_add_func("/bench/latex/table/small",                bench_generate_table_small);
    g_test_add_func("/bench/latex/table/large",                bench_generate_table_large);
    g_test_add_func("/bench/latex/matrix/small",               bench_generate_matrix_small);
    g_test_add_func("/bench/latex/matrix/large",               bench_generate_matrix_large);
    g_test_add_func("/bench/latex/image",                      bench_generate_image);

    /* utils — subinstr */
    g_test_add_func("/bench/utils/subinstr/hit",               bench_subinstr_hit);
    g_test_add_func("/bench/utils/subinstr/miss",              bench_subinstr_miss);
    g_test_add_func("/bench/utils/subinstr/icase",             bench_subinstr_icase);
    g_test_add_func("/bench/utils/subinstr/long_haystack",     bench_subinstr_long_haystack);
    g_test_add_func("/bench/utils/subinstr/long_icase",        bench_subinstr_long_haystack_icase);
    g_test_add_func("/bench/utils/subinstr/miss_full_scan",    bench_subinstr_miss_full_scan);

    /* utils — g_substr */
    g_test_add_func("/bench/utils/g_substr/small",             bench_g_substr);
    g_test_add_func("/bench/utils/g_substr/large",             bench_g_substr_large);

    /* utils — path_exists */
    g_test_add_func("/bench/utils/path_exists/hit",            bench_utils_path_exists_true);
    g_test_add_func("/bench/utils/path_exists/miss",           bench_utils_path_exists_false);

    /* slist */
    g_test_add_func("/bench/slist/find_head",                  bench_slist_find_head);
    g_test_add_func("/bench/slist/find_tail",                  bench_slist_find_tail);
    g_test_add_func("/bench/slist/find_prefix_mid",            bench_slist_find_prefix_mid);
    g_test_add_func("/bench/slist/build_free",                 bench_slist_build_free);

    /* snippets */
    g_test_add_func("/bench/snippets/new_unref",               bench_snippets_new_unref);
    g_test_add_func("/bench/snippets/reload",                  bench_snippets_reload);

    /* config */
    g_test_add_func("/bench/config/get_string",                bench_config_get_string);
    g_test_add_func("/bench/config/set_string",                bench_config_set_string);
    g_test_add_func("/bench/config/many_unique_keys",          bench_config_many_unique_keys);
    g_test_add_func("/bench/config/get_string_realistic",      bench_config_get_string_realistic);

    g_test_add_func("/bench/sink",                             bench_drain_sink);

    return g_test_run();
}
