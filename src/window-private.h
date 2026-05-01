/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "compiler.h"
#include "editor.h"
#include "git.h"
#include "preview.h"
#include "searchbar.h"
#include "snippets.h"
#include "structure.h"
#include "window.h"

G_BEGIN_DECLS

/*
 * Layout constants — keep editor and preview usable side-by-side.
 * The window minimum width is derived so a horizontal GtkPaned never ends up
 * with zero room for one of the panes (which breaks the layout on small sizes).
 */
#define SILKTEX_EDITOR_MIN_WIDTH       50
#define SILKTEX_PREVIEW_PANE_MIN_WIDTH 150
#define SILKTEX_WINDOW_MIN_WIDTH       (SILKTEX_EDITOR_MIN_WIDTH + SILKTEX_PREVIEW_PANE_MIN_WIDTH + 72)
#define SILKTEX_WINDOW_MIN_HEIGHT      400

struct _SilktexWindow {
    AdwApplicationWindow parent_instance;

    AdwToolbarView *root_toolbar_view;
    AdwWindowTitle *window_title;
    AdwToastOverlay *toast_overlay;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    AdwOverlaySplitView *split_view;
    GtkPaned *editor_paned;
    GtkPaned *log_paned;
    AdwToolbarView *editor_toolbar_view;
    GtkBox *editor_bottom_bar;
    AdwToolbarView *preview_toolbar_view;
    GtkBox *preview_box;
    GtkBox *structure_container;
    GtkLabel *page_label;
    GtkLabel *preview_status;
    GtkToggleButton *btn_preview;
    GtkToggleButton *btn_sidebar;
    GtkButton *btn_compile;
    GtkToggleButton *btn_tools_toggle;
    GtkMenuButton *btn_menu;
    GtkMenuButton *btn_collab;
    GtkMenuButton *btn_git_menu;
    GtkButton *btn_save;
    GtkToggleButton *btn_log;

    SilktexPreview *preview;
    SilktexCompiler *compiler;
    SilktexSearchbar *searchbar;
    SilktexSnippets *snippets;
    SilktexStructure *structure;

    GtkWidget *log_scroll;
    GtkTextBuffer *log_buf;
    GtkWidget *log_text_view;

    /* Theme swatch toggles — owned by window-primary-menu.c popover */

    GtkToggleButton *theme_follow;
    GtkToggleButton *theme_light;
    GtkToggleButton *theme_dark;

    SilktexGitStatus *git_status;
    char *git_status_message;
    char *last_tex_save_dir;
    AdwDialog *git_dialog;
    GtkLabel *git_branch_label;
    GtkLabel *git_repo_label;
    GtkListBox *git_list;
    GtkEditable *git_commit_message;

    guint compile_timer_id;
    guint autosave_timer_id;
    gboolean auto_compile;
    gboolean is_fullscreen;
    gboolean preview_narrow;
    gboolean preview_auto_collapsed;

    /*
     * Horizontal split between editor (start) and PDF preview (end).
     * preview_pane_silence: skip notify::position handler while moving the
     *   grip programmatically to avoid feedback loops.
     * preview_split_seeded: ignore Gtk's default position until we apply 50%
     *   or a restored value (otherwise user drag is overwritten).
     */
    gint preview_pane_pos;
    gdouble preview_pane_ratio;
    gboolean preview_pane_restorable;
    gboolean preview_pane_silence;
    gboolean preview_split_seeded;

    /* Single high-priority toast; dismiss before showing another */

    AdwToast *current_toast;
};

SilktexEditor *silktex_window_editor_for_page(AdwTabPage *page);

void silktex_window_update_window_title(SilktexWindow *self);
void silktex_window_update_tab_title(SilktexWindow *self, AdwTabPage *page, SilktexEditor *editor);
void silktex_window_update_page_label(SilktexWindow *self);
void silktex_window_update_log_panel(SilktexWindow *self);

void silktex_window_apply_theme_to_editor(SilktexEditor *editor);
void silktex_window_apply_theme_to_all_editors(SilktexWindow *self);
void silktex_window_apply_preview_theme(SilktexWindow *self);

void silktex_window_focus_active_editor(SilktexWindow *self);

void silktex_window_restart_compile_timer(SilktexWindow *self);
void silktex_window_restart_autosave_timer(SilktexWindow *self);

void silktex_window_apply_editor_paned_half_split(SilktexWindow *self);

GtkWidget *silktex_window_create_editor_page(SilktexWindow *self, SilktexEditor *editor);

void silktex_window_on_prefs_apply(gpointer user_data);

void silktex_window_install_chrome_css(void);

void silktex_window_git_register_actions(SilktexWindow *self);
void silktex_window_git_refresh_state(SilktexWindow *self);
void silktex_window_git_update_actions(SilktexWindow *self);

void silktex_window_apply_theme_from_config(void);
void silktex_window_install_primary_menu(SilktexWindow *self);
void silktex_window_register_menu_actions(SilktexWindow *self);
void silktex_window_connect_theme_follow(SilktexWindow *self);

G_END_DECLS
