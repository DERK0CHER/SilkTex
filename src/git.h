/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Plain C API for git operations and parsed status (branch, file list with
 * index/worktree codes). All functions are main-thread-safe only in the sense
 * that they block until git exits — do not call from GTK callbacks directly.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    char *path;
    char index_status;
    char worktree_status;
} SilktexGitFile;

typedef struct {
    char *repo_root;
    char *branch;
    GPtrArray *files; /* SilktexGitFile* */
} SilktexGitStatus;

gboolean silktex_git_is_available(void);

SilktexGitStatus *silktex_git_status_load(const char *path, GError **error);
void silktex_git_status_free(SilktexGitStatus *status);

gboolean silktex_git_stage_file(const char *repo_root, const char *path, GError **error);
gboolean silktex_git_unstage_file(const char *repo_root, const char *path, GError **error);
gboolean silktex_git_commit(const char *repo_root, const char *message, char **output, GError **error);
gboolean silktex_git_pull(const char *repo_root, char **output, GError **error);
gboolean silktex_git_push(const char *repo_root, char **output, GError **error);

G_END_DECLS
