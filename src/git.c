/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "git.h"
#include <glib/gstdio.h>

static void silktex_git_file_free(gpointer data)
{
    SilktexGitFile *file = data;
    if (file == NULL) return;

    g_free(file->path);
    g_free(file);
}

gboolean silktex_git_is_available(void)
{
    g_autofree char *git = g_find_program_in_path("git");
    return git != NULL;
}

static gboolean run_git(const char *cwd, const char *const argv[], char **output, GError **error)
{
    g_autofree char *stdout_buf = NULL;
    g_autofree char *stderr_buf = NULL;
    int exit_status = 0;

    gboolean spawned = g_spawn_sync(cwd, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                                   &stdout_buf, &stderr_buf, &exit_status, error);
    if (!spawned) return FALSE;

    if (!g_spawn_check_wait_status(exit_status, error)) {
        if (error != NULL && *error != NULL && stderr_buf != NULL && *stderr_buf != '\0') {
            g_prefix_error(error, "%s: ", g_strstrip(stderr_buf));
        }
        return FALSE;
    }

    if (output != NULL) {
        *output = g_steal_pointer(&stdout_buf);
    }

    return TRUE;
}

static char *path_to_workdir(const char *path)
{
    if (path == NULL || *path == '\0') return NULL;

    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return g_strdup(path);
    }

    return g_path_get_dirname(path);
}

static char *discover_root(const char *path, GError **error)
{
    g_autofree char *cwd = path_to_workdir(path);
    if (cwd == NULL) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Save the file before using Git");
        return NULL;
    }

    const char *argv[] = {"git", "rev-parse", "--show-toplevel", NULL};
    g_autofree char *output = NULL;
    if (!run_git(cwd, argv, &output, error)) return NULL;

    return g_strdup(g_strstrip(output));
}

static char *parse_branch(const char *line)
{
    const char *branch = line + 3;
    if (g_str_has_prefix(branch, "No commits yet on ")) {
        branch += strlen("No commits yet on ");
    }

    const char *end = strstr(branch, "...");
    if (end == NULL) end = strchr(branch, ' ');
    if (end == NULL) end = branch + strlen(branch);

    return g_strndup(branch, end - branch);
}

static void add_status_file(SilktexGitStatus *status, const char *line)
{
    if (line == NULL || strlen(line) < 4) return;

    SilktexGitFile *file = g_new0(SilktexGitFile, 1);
    file->index_status = line[0];
    file->worktree_status = line[1];

    const char *path = line + 3;
    const char *rename_target = strstr(path, " -> ");
    file->path = g_strdup(rename_target != NULL ? rename_target + 4 : path);

    g_ptr_array_add(status->files, file);
}

SilktexGitStatus *silktex_git_status_load(const char *path, GError **error)
{
    if (!silktex_git_is_available()) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT, "git is not installed");
        return NULL;
    }

    g_autofree char *repo_root = discover_root(path, error);
    if (repo_root == NULL) return NULL;

    const char *argv[] = {"git", "status", "--porcelain=v1", "-b", NULL};
    g_autofree char *output = NULL;
    if (!run_git(repo_root, argv, &output, error)) return NULL;

    SilktexGitStatus *status = g_new0(SilktexGitStatus, 1);
    status->repo_root = g_steal_pointer(&repo_root);
    status->branch = g_strdup("HEAD");
    status->files = g_ptr_array_new_with_free_func(silktex_git_file_free);

    g_auto(GStrv) lines = g_strsplit(output ? output : "", "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (*lines[i] == '\0') continue;
        if (g_str_has_prefix(lines[i], "## ")) {
            g_free(status->branch);
            status->branch = parse_branch(lines[i]);
        } else {
            add_status_file(status, lines[i]);
        }
    }

    return status;
}

void silktex_git_status_free(SilktexGitStatus *status)
{
    if (status == NULL) return;

    g_free(status->repo_root);
    g_free(status->branch);
    g_clear_pointer(&status->files, g_ptr_array_unref);
    g_free(status);
}

gboolean silktex_git_stage_file(const char *repo_root, const char *path, GError **error)
{
    const char *argv[] = {"git", "add", "--", path, NULL};
    return run_git(repo_root, argv, NULL, error);
}

gboolean silktex_git_unstage_file(const char *repo_root, const char *path, GError **error)
{
    const char *argv[] = {"git", "restore", "--staged", "--", path, NULL};
    return run_git(repo_root, argv, NULL, error);
}

gboolean silktex_git_commit(const char *repo_root, const char *message, char **output, GError **error)
{
    const char *argv[] = {"git", "commit", "-m", message, NULL};
    return run_git(repo_root, argv, output, error);
}

gboolean silktex_git_pull(const char *repo_root, char **output, GError **error)
{
    const char *argv[] = {"git", "pull", "--ff-only", NULL};
    return run_git(repo_root, argv, output, error);
}

gboolean silktex_git_push(const char *repo_root, char **output, GError **error)
{
    const char *argv[] = {"git", "push", NULL};
    return run_git(repo_root, argv, output, error);
}
