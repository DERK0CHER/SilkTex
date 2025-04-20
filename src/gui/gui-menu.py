#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Menu functionality GUI module for Gummi.

Copyright (C) 2009-2025 Gummi Developers
All Rights reserved.

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
"""

import os
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Gdk, GLib

from configfile import (config_set_boolean, config_set_integer)
from constants import (C_TMPDIR, C_PACKAGE_NAME, C_PACKAGE_VERSION, C_PACKAGE_URL,
                      C_PACKAGE_COPYRIGHT, C_PACKAGE_LICENSE, C_PACKAGE_COMMENTS,
                      C_PACKAGE_GUIDE, C_CREDITS_DEVELOPERS, C_CREDITS_DOCUMENTERS,
                      C_CREDITS_TRANSLATORS, RECENT_FILES_NUM, TEXCOUNT_OUTPUT_LINES)
from editor import (editor_undo_change, editor_redo_change, editor_jumpto_search_result,
                   editor_activate_spellchecking)
from environment import gummi_project_active, gummi_get_all_editors
from external import external_exists
from project import (project_create_new, project_open_existing, project_close)
from gui.gui_main import (gui_open_file, gui_save_file, gui_set_hastabs_sensitive,
                         gui_set_filename_display, check_for_save, display_recent_files)
from gui.gui_preview import previewgui_start_errormode
from gui.gui_project import (projectgui_enable, projectgui_disable, projectgui_list_projfiles)
from gui.gui_search import searchgui_main
from gui.gui_prefs import prefsgui_main
from latex import (latex_export_pdffile, latex_remove_auxfile, latex_run_makeindex)
from utils import (utils_popen_r, utils_copy_file, utils_path_exists, get_open_filename,
                  get_save_filename, slog, statusbar_set_message, LogLevel, Tuple2, TYPE_LATEX, 
                  TYPE_PDF, TYPE_PROJECT)


class MenuGui:
    """Menu GUI handling class for Gummi."""
    
    def __init__(self, builder, gummi, gui):
        """Initialize the menu GUI.
        
        Args:
            builder: Gtk.Builder instance with the UI definitions
            gummi: Reference to the main Gummi instance
            gui: Reference to the main GUI instance
        """
        self.gummi = gummi
        self.gui = gui
        
        # Get widgets from builder
        self.menu_projcreate = builder.get_object("menu_projcreate")
        self.menu_projopen = builder.get_object("menu_projopen")
        self.menu_projclose = builder.get_object("menu_projclose")
        self.menu_cut = builder.get_object("menu_cut")
        self.menu_copy = builder.get_object("menu_copy")
        
        # TODO: There has to be a better way than this.. (bug 246)
        # In GTK4, this would be handled differently
        # settings = Gtk.Settings.get_default()
        # iconsizes = settings.get_property("gtk-icon-sizes")
        # if iconsizes:
        #     print(f"{iconsizes}")


    # FILE MENU

    def on_menu_new_activate(self, widget, user_data=None):
        """Handle activation of the New menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if not self.gui.rightpane.get_sensitive():
            gui_set_hastabs_sensitive(True)
        self.gummi.tabmanager.create_tab(0, None, None)  # A_NONE is 0
    
    def on_menu_template_activate(self, widget, user_data=None):
        """Handle activation of the Template menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        self.gummi.templ.list_templates.clear()
        self.gummi.templ.setup()
        self.gummi.templ.templatewindow.set_visible(True)
    
    def on_menu_open_activate(self, widget, user_data=None):
        """Handle activation of the Open menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        filename = get_open_filename(TYPE_LATEX)
        if filename:
            gui_open_file(filename)
        
        if self.gummi.active_editor:
            self.gummi.active_editor.view.grab_focus()
    
    def on_menu_save_activate(self, widget, user_data=None):
        """Handle activation of the Save menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        gui_save_file(self.gummi.active_tab, False)
    
    def on_menu_saveas_activate(self, widget, user_data=None):
        """Handle activation of the Save As menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        gui_save_file(self.gummi.active_tab, True)
    
    def on_menu_export_activate(self, widget, user_data=None):
        """Handle activation of the Export menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        filename = get_save_filename(TYPE_PDF)
        if filename:
            latex_export_pdffile(self.gummi.latex, self.gummi.active_editor, filename, True)
    
    def on_menu_recent_activate(self, widget, user_data=None):
        """Handle activation of a recent file menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        name = widget.get_label()
        index = ord(name[0]) - ord('0') - 1
        
        if utils_path_exists(self.gui.recent_list[index]):
            gui_open_file(self.gui.recent_list[index])
        else:
            error_msg = _("Error loading recent file: {0}").format(self.gui.recent_list[index])
            slog(LogLevel.ERROR, f"{error_msg}")
            slog(LogLevel.G_ERROR, f"Could not find the file {self.gui.recent_list[index]}.")
            statusbar_set_message(error_msg)
            
            self.gui.recent_list[index] = None
            
            # Shift the remaining items up
            while index < RECENT_FILES_NUM - 1:
                self.gui.recent_list[index] = self.gui.recent_list[index + 1]
                index += 1
            
            self.gui.recent_list[RECENT_FILES_NUM - 1] = "__NULL__"
        
        display_recent_files(self.gui)
    
    def on_menu_close_activate(self, widget, user_data=None):
        """Handle activation of the Close menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (Tab context if specified)
        """
        tab = user_data if user_data else self.gummi.active_tab
        ret = check_for_save(tab.editor)
        
        if ret == Gtk.ResponseType.YES:
            gui_save_file(tab, False)
        elif ret == Gtk.ResponseType.CANCEL or ret == Gtk.ResponseType.DELETE_EVENT:
            return
        
        # Kill typesetter thread
        self.gummi.motion.kill_typesetter()
        
        # Remove tab
        remaining_tabs = self.gummi.tabmanager.remove_tab(tab)
        
        if remaining_tabs == 0:
            previewgui_start_errormode(self.gui.previewgui, "")
            gui_set_hastabs_sensitive(False)
        else:
            gui_set_filename_display(self.gummi.active_tab, True, False)
    
    def on_menu_quit_activate(self):
        """Handle activation of the Quit menu item.
        
        Returns:
            True if quit operation was cancelled, False otherwise
        """
        length = len(self.gummi.tabmanager.tabs)
        
        self.gummi.motion.pause_compile_thread()
        
        for i in range(length):
            self.gui.tabmanagergui.notebook.set_current_page(i)
            self.gummi.tabmanager.set_active_tab(i)
            ret = check_for_save(self.gummi.active_editor)
            
            if ret == Gtk.ResponseType.YES:
                gui_save_file(self.gummi.active_tab, False)
            elif ret == Gtk.ResponseType.CANCEL or ret == Gtk.ResponseType.DELETE_EVENT:
                return True
        
        # Stop compile thread
        if length > 0:
            self.gummi.motion.stop_compile_thread()
        
        # Save current window size/position to persistent config
        if self.gui.mainwindow.is_maximized():
            config_set_boolean("Interface", "mainwindow_max", True)
        else:
            # Unmaximized mainwindow
            width, height = self.gui.mainwindow.get_size()
            x, y = self.gui.mainwindow.get_position()
            
            config_set_boolean("Interface", "mainwindow_max", False)
            config_set_integer("Interface", "mainwindow_x", x)
            config_set_integer("Interface", "mainwindow_y", y)
            config_set_integer("Interface", "mainwindow_w", width)
            config_set_integer("Interface", "mainwindow_h", height)
        
        Gtk.main_quit()
        
        # Destroy editors
        for i in range(length):
            self.gummi.tabmanager.tabs[i].editor.destroy()
        
        print(" ___ ")
        print(" {o,o} Thanks for using Gummi!")
        print(" |)__) I welcome your feedback at:")
        print(" -\"-\"- https://github.com/alexandervdm/gummi\n")
        
        return False

    # EDIT MENU

    def on_menu_edit_activate(self, widget, user_data=None):
        """Handle activation of the Edit menu.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if not self.gummi.active_editor:
            return
        
        if self.gummi.active_editor.buffer.get_has_selection():
            self.menu_cut.set_sensitive(True)
            self.menu_copy.set_sensitive(True)
            return
        
        self.menu_cut.set_sensitive(False)
        self.menu_copy.set_sensitive(False)
    
    def on_menu_undo_activate(self, widget, user_data=None):
        """Handle activation of the Undo menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        editor_undo_change(self.gummi.active_editor)
    
    def on_menu_redo_activate(self, widget, user_data=None):
        """Handle activation of the Redo menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        editor_redo_change(self.gummi.active_editor)
    
    def on_menu_cut_activate(self, widget, user_data=None):
        """Handle activation of the Cut menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        clipboard = Gdk.Display.get_default().get_clipboard()
        self.gummi.active_editor.buffer.cut_clipboard(clipboard, True)
    
    def on_menu_copy_activate(self, widget, user_data=None):
        """Handle activation of the Copy menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        clipboard = Gdk.Display.get_default().get_clipboard()
        self.gummi.active_editor.buffer.copy_clipboard(clipboard)
    
    def on_menu_paste_activate(self, widget, user_data=None):
        """Handle activation of the Paste menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        clipboard = Gdk.Display.get_default().get_clipboard()
        self.gummi.active_editor.buffer.paste_clipboard(clipboard, None, True)
    
    def on_menu_delete_activate(self, widget, user_data=None):
        """Handle activation of the Delete menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        self.gummi.active_editor.buffer.delete_selection(False, True)
    
    def on_menu_selectall_activate(self, widget, user_data=None):
        """Handle activation of the Select All menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        start, end = self.gummi.active_editor.buffer.get_bounds()
        self.gummi.active_editor.buffer.select_range(start, end)
    
    def on_menu_preferences_activate(self, widget, user_data=None):
        """Handle activation of the Preferences menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        prefsgui_main(self.gui.prefsgui, 0)

    # VIEW MENU

    def on_menu_statusbar_toggled(self, widget, user_data=None):
        """Handle toggling of the statusbar menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if widget.get_active():
            self.gui.statusbar.set_visible(True)
            config_set_boolean("Interface", "statusbar", True)
        else:
            self.gui.statusbar.set_visible(False)
            config_set_boolean("Interface", "statusbar", False)
    
    def on_menu_toolbar_toggled(self, widget, user_data=None):
        """Handle toggling of the toolbar menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if widget.get_active():
            self.gui.toolbar.set_visible(True)
            config_set_boolean("Interface", "toolbar", True)
        else:
            self.gui.toolbar.set_visible(False)
            config_set_boolean("Interface", "toolbar", False)
    
    def on_menu_rightpane_toggled(self, widget, user_data=None):
        """Handle toggling of the right pane menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if widget.get_active():
            self.gui.rightpane.set_visible(True)
            config_set_boolean("Interface", "rightpane", True)
            self.gui.previewgui.preview_pause.set_active(False)
        else:
            self.gui.rightpane.set_visible(False)
            config_set_boolean("Interface", "rightpane", False)
            self.gui.previewgui.preview_pause.set_active(True)
    
    def on_menu_fullscreen_toggled(self, widget, user_data=None):
        """Handle toggling of the fullscreen menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if widget.get_active():
            self.gui.mainwindow.fullscreen()
        else:
            self.gui.mainwindow.unfullscreen()

    # SEARCH MENU

    def on_menu_find_activate(self, widget, user_data=None):
        """Handle activation of the Find menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        searchgui_main(self.gui.searchgui)
    
    def on_menu_findnext_activate(self, widget, user_data=None):
        """Handle activation of the Find Next menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        editor_jumpto_search_result(self.gummi.active_editor, 1)
    
    def on_menu_findprev_activate(self, widget, user_data=None):
        """Handle activation of the Find Previous menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        editor_jumpto_search_result(self.gummi.active_editor, -1)

    # DOCUMENT MENU

    def on_menu_pdfcompile_activate(self, widget, user_data=None):
        """Handle activation of the PDF Compile menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        self.gummi.latex.modified_since_compile = True
        self.gummi.motion.do_compile()
    
    def on_menu_compileopts_activate(self, widget, user_data=None):
        """Handle activation of the Compile Options menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        prefsgui_main(self.gui.prefsgui, 4)
    
    def on_menu_cleanup_activate(self, widget, user_data=None):
        """Handle activation of the Cleanup Build Files menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        result = latex_remove_auxfile(self.gummi.active_editor)
        if result == 0:
            statusbar_set_message(_("Successfully removed build files.."))
        else:
            statusbar_set_message(_("Error removing build files.."))
    
    def on_menu_runmakeindex_activate(self, widget, user_data=None):
        """Handle activation of the Run Makeindex menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if latex_run_makeindex(self.gummi.active_editor):
            statusbar_set_message(_("Running Makeindex.."))
        else:
            statusbar_set_message(_("Error running Makeindex.."))
        
        self.gummi.motion.force_compile()
    
    def on_menu_runbibtex_activate(self, widget, user_data=None):
        """Handle activation of the Run BibTeX menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        self.gui.on_button_biblio_compile_clicked(widget, user_data)
    
    def on_menu_docstat_activate(self, widget, user_data=None):
        """Handle activation of the Document Statistics menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        import re
        
        # Terms for the document statistics
        terms = [
            _("Words in text"),
            _("Words in headers"),
            _("Words in float captions"),
            _("Number of headers"),
            _("Number of floats"),
            _("Number of math inlines"),
            _("Number of math displayed")
        ]
        
        terms_regex = [
            r"Words in text: ([0-9]*)",
            r"Words in headers: ([0-9]*)",
            r"Words in float captions: ([0-9]*)",
            r"Number of headers: ([0-9]*)",
            r"Number of floats: ([0-9]*)",
            r"Number of math inlines: ([0-9]*)",
            r"Number of math displayed: ([0-9]*)"
        ]
        
        # TODO: move to non-gui class (latex perhaps)
        if external_exists("texcount"):
            # Copy workfile to /tmp to remove any spaces in filename to avoid segfaults
            tmpfile = f"{self.gummi.active_editor.fdname}.state"
            try:
                if not utils_copy_file(self.gummi.active_editor.workfile, tmpfile, None):
                    slog(LogLevel.G_ERROR, f"utils_copy_file(): Error copying file")
                    return
                
                cmd = f"texcount '{tmpfile}'"
                result = utils_popen_r(cmd, None)
                
                regexs = []
                res = [None] * TEXCOUNT_OUTPUT_LINES
                
                for i in range(TEXCOUNT_OUTPUT_LINES):
                    try:
                        regexs.append(re.compile(terms_regex[i]))
                    except re.error as err:
                        slog(LogLevel.G_ERROR, f"regex compile error: {err}")
                        os.unlink(tmpfile)
                        return
                
                for i in range(TEXCOUNT_OUTPUT_LINES):
                    match = regexs[i].search(result[1])
                    if match:
                        if match.group(1) is None:
                            slog(LogLevel.WARNING, f"can't extract info: {terms[i]}")
                            res[i] = "N/A"
                        else:
                            res[i] = match.group(1)
                
                os.unlink(tmpfile)
                
            except Exception as err:
                slog(LogLevel.G_ERROR, f"Error: {err}")
                return
        else:
            slog(LogLevel.G_ERROR, "The 'texcount' utility could not be found.")
            return
        
        # Update the statistics labels in the UI
        items = ["stats_words", "stats_head", "stats_float", 
                "stats_nrhead", "stats_nrfloat", "stats_nrmath"]
        
        for j in range(6):
            value = items[j]
            tmp = self.gui.builder.get_object(value)
            tmp.set_text(res[j])
        
        self.gui.builder.get_object("stats_filename").set_text(
            self.gui.tabmanagergui.get_labeltext(self.gummi.active_tab.page))
        
        self.gui.docstatswindow.show()
    
    def on_menu_spelling_toggled(self, widget, user_data=None):
        """Handle toggling of the spelling menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        active_status = widget.get_active()
        editors = gummi_get_all_editors()
        
        for ec in editors:
            editor_activate_spellchecking(ec, active_status)
        
        config_set_boolean("Editor", "spelling", active_status)
    
    def on_menu_snippets_toggled(self, widget, user_data=None):
        """Handle toggling of the snippets menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if widget.get_active():
            slog(LogLevel.INFO, "Snippets activated")
            config_set_boolean("Interface", "snippets", True)
        else:
            slog(LogLevel.INFO, "Snippets deactivated")
            config_set_boolean("Interface", "snippets", False)

    # PROJECT MENU

    def on_menu_project_activate(self, widget, user_data=None):
        """Handle activation of the Project menu.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        # TODO: perhaps use buffer to run pre-compile check
        if not self.gummi.active_editor:
            self.menu_projcreate.set_tooltip_text(
                _("This function requires an active document"))
            return
        
        if not gummi_project_active():
            self.menu_projopen.set_sensitive(True)
            
            # TODO: we should probably have functions for calls like this
            if self.gummi.active_editor.filename is not None:
                self.menu_projcreate.set_sensitive(True)
            else:
                self.menu_projcreate.set_tooltip_text(
                    _("This function requires the current\n"
                      "active document to be saved. "))
        else:
            self.menu_projclose.set_sensitive(True)
    
    def on_menu_project_deselect(self, widget, user_data=None):
        """Handle deselection of the Project menu.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        self.menu_projcreate.set_sensitive(False)
        self.menu_projopen.set_sensitive(False)
        self.menu_projclose.set_sensitive(False)
    
    def on_menu_projcreate_activate(self, widget, user_data=None):
        """Handle activation of the Create Project menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        filename = get_save_filename(TYPE_PROJECT)
        if not filename:
            return
        
        if project_create_new(filename):
            projectgui_enable(self.gummi.project, self.gui.projectgui)
            projectgui_list_projfiles(self.gummi.project.projfile)
    
    def on_menu_projopen_activate(self, widget, user_data=None):
        """Handle activation of the Open Project menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        filename = get_open_filename(TYPE_PROJECT)
        if not filename:
            return
        
        if project_open_existing(filename):
            statusbar_set_message(f"Loading project {filename}")
            projectgui_enable(self.gummi.project, self.gui.projectgui)
            projectgui_list_projfiles(self.gummi.project.projfile)
        else:
            statusbar_set_message(f"An error occurred while loading project {filename}")
    
    def on_menu_projclose_activate(self, widget, user_data=None):
        """Handle activation of the Close Project menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        if not self.gummi.project.projfile:
            return
        
        if project_close():
            projectgui_disable(self.gummi.project, self.gui.projectgui)

    # HELP MENU

    def on_menu_guide_activate(self, widget, user_data=None):
        """Handle activation of the User Guide menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        try:
            Gtk.show_uri(None, C_PACKAGE_GUIDE, Gdk.CURRENT_TIME)
        except GLib.Error as error:
            slog(LogLevel.ERROR, f"Can't open user guide: {error.message}")
    
    def on_menu_about_activate(self, widget, user_data=None):
        """Handle activation of the About menu item.
        
        Args:
            widget: The menu item widget
            user_data: Additional data (unused)
        """
        try:
            icon_file = os.path.join(os.path.dirname(__file__), "icons", "gummi.png")
            icon = Gtk.Image.new_from_file(icon_file).get_pixbuf()
        except Exception:
            icon = None
        
        dialog = Gtk.AboutDialog()
        dialog.set_transient_for(self.gui.mainwindow)
        dialog.set_destroy_with_parent(True)
        
        dialog.set_authors(C_CREDITS_DEVELOPERS)
        dialog.set_program_name(C_PACKAGE_NAME)
        dialog.set_version(C_PACKAGE_VERSION)
        dialog.set_website(C_PACKAGE_URL)
        dialog.set_copyright(C_PACKAGE_COPYRIGHT)
        dialog.set_license(C_PACKAGE_LICENSE)
        dialog.set_logo(icon)
        dialog.set_comments(C_PACKAGE_COMMENTS)
        dialog.set_translator_credits(C_CREDITS_TRANSLATORS)
        dialog.set_documenters(C_CREDITS_DOCUMENTERS)
        
        dialog.run()
        dialog.destroy()


def menugui_init(builder, gummi, gui):
    """Initialize and return a new MenuGui instance.
    
    Args:
        builder: Gtk.Builder instance with the UI definitions
        gummi: Reference to the main Gummi instance
        gui: Reference to the main GUI instance
        
    Returns:
        A new MenuGui instance
    """
    if not isinstance(builder, Gtk.Builder):
        return None
    
    return MenuGui(builder, gummi, gui)