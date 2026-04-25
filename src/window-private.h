/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SilktexWindow — internal instance layout and cross-unit API.
 *
 * The public type is declared in window.h (opaque pointer). This header
 * completes struct _SilktexWindow so that the main window implementation
 * (window.c) and satellite units (window-git.c, window-primary-menu.c) can
 * share one GObject type without circular includes.
 *
 * Include this header only from those .c files — never from public headers
 * shipped to other modules.
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
#define SILKTEX_EDITOR_MIN_WIDTH 280
#define SILKTEX_PREVIEW_PANE_MIN_WIDTH 200
#define SILKTEX_WINDOW_MIN_WIDTH (SILKTEX_EDITOR_MIN_WIDTH + SILKTEX_PREVIEW_PANE_MIN_WIDTH + 72)
#define SILKTEX_WINDOW_MIN_HEIGHT 400

struct _SilktexWindow {
    AdwApplicationWindow parent_instance;

    /* ---- Template widgets (main.ui) ---- */

    AdwToolbarView *root_toolbar_view;
    AdwWindowTitle *window_title;
    AdwToastOverlay *toast_overlay;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    AdwOverlaySplitView *split_view;
    GtkPaned *editor_paned;
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
    GtkMenuButton *btn_menu;

    /* ---- Core subsystems (not from template) ---- */

    SilktexPreview *preview;
    SilktexCompiler *compiler;
    SilktexSearchbar *searchbar;
    SilktexSnippets *snippets;
    SilktexStructure *structure;

    /* Compile log: revealer + text view built in window init */

    GtkRevealer *log_revealer;
    GtkTextBuffer *log_buf;
    GtkToggleButton *log_toggle;
    GtkWidget *log_text_view;

    /* Theme swatch toggles — owned by window-primary-menu.c popover */

    GtkToggleButton *theme_follow;
    GtkToggleButton *theme_light;
    GtkToggleButton *theme_dark;
    GtkToggleButton *theme_lightsout;

    SilktexGitStatus *git_status;
    char *git_status_message;
    AdwDialog *git_dialog;
    GtkLabel *git_branch_label;
    GtkLabel *git_repo_label;
    GtkListBox *git_list;
    GtkEditable *git_commit_message;

    /* ---- Debounced / responsive behaviour ---- */

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
    gboolean preview_pane_restorable;
    gboolean preview_pane_silence;
    gboolean preview_split_seeded;

    /* Single high-priority toast; dismiss before showing another */

    AdwToast *current_toast;
};

/* ---- Shared helpers implemented in window.c ---- */

SilktexEditor *silktex_window_editor_for_page(AdwTabPage *page);

void silktex_window_update_window_title(SilktexWindow *self);
void silktex_window_update_tab_title(SilktexWindow *self, AdwTabPage *page, SilktexEditor *editor);
void silktex_window_update_page_label(SilktexWindow *self);
void silktex_window_update_log_panel(SilktexWindow *self);

void silktex_window_apply_theme_to_editor(SilktexEditor *editor);
void silktex_window_apply_theme_to_all_editors(SilktexWindow *self);

void silktex_window_focus_active_editor(SilktexWindow *self);

void silktex_window_restart_compile_timer(SilktexWindow *self);
void silktex_window_restart_autosave_timer(SilktexWindow *self);

void silktex_window_apply_editor_paned_half_split(SilktexWindow *self);

GtkWidget *silktex_window_create_editor_page(SilktexWindow *self, SilktexEditor *editor);

void silktex_window_on_prefs_apply(gpointer user_data);

void silktex_window_install_chrome_css(void);

/* ---- Git UI + actions (window-git.c) ---- */

void silktex_window_git_register_actions(SilktexWindow *self);
void silktex_window_git_refresh_state(SilktexWindow *self);
void silktex_window_git_update_actions(SilktexWindow *self);

/* ---- Primary menu, theme, recent files (window-primary-menu.c) ---- */

void silktex_window_apply_theme_from_config(void);
void silktex_window_install_primary_menu(SilktexWindow *self);
void silktex_window_register_menu_actions(SilktexWindow *self);
void silktex_window_connect_theme_follow(SilktexWindow *self);

G_END_DECLS
