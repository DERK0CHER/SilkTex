#!/usr/bin/env python3
"""
Menu functionality GUI module for Gummi.
Copyright (C) 2009-2025 Gummi Developers
All Rights reserved.
"""

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Gdk, GLib, GdkPixbuf
import os
import re
from enum import Enum

# Constants from your application
GUMMI_DATA = "/usr/share/gummi"
TEXCOUNT_OUTPUT_LINES = 7
TYPE_LATEX = 1
TYPE_PDF = 2
TYPE_PROJECT = 3

class MenuGui:
    """Menu GUI handling class for Gummi."""
    
    def __init__(self, builder, main_window):
        """Initialize the menu GUI.
        
        Args:
            builder: Gtk.Builder instance with the UI definitions
            main_window: Main application window
        """
        self.builder = builder
        self.main_window = main_window
        
        # Get menu widgets from builder
        self.menu_projcreate = builder.get_object("menu_projcreate")
        self.menu_projopen = builder.get_object("menu_projopen")
        self.menu_projclose = builder.get_object("menu_projclose")
        self.menu_cut = builder.get_object("menu_cut")
        self.menu_copy = builder.get_object("menu_copy")
        
        # Connect all menu signals
        self.connect_menu_signals()
        
        # Initialize any needed state
        self.recent_list = ["__NULL__"] * 10  # Assuming max 10 recent files
        
    def connect_menu_signals(self):
        """Connect all menu signal handlers"""
        signals = {
            # File menu
            "on_menu_new_activate": self.on_menu_new_activate,
            "on_menu_template_activate": self.on_menu_template_activate,
            "on_menu_open_activate": self.on_menu_open_activate,
            "on_menu_save_activate": self.on_menu_save_activate,
            "on_menu_saveas_activate": self.on_menu_saveas_activate,
            "on_menu_export_activate": self.on_menu_export_activate,
            "on_menu_recent_activate": self.on_menu_recent_activate,
            "on_menu_close_activate": self.on_menu_close_activate,
            "on_menu_quit_activate": self.on_menu_quit_activate,

            # Edit menu  
            "on_menu_edit_activate": self.on_menu_edit_activate,
            "on_menu_undo_activate": self.on_menu_undo_activate,
            "on_menu_redo_activate": self.on_menu_redo_activate,
            "on_menu_cut_activate": self.on_menu_cut_activate,
            "on_menu_copy_activate": self.on_menu_copy_activate,
            "on_menu_paste_activate": self.on_menu_paste_activate,
            "on_menu_delete_activate": self.on_menu_delete_activate,
            "on_menu_selectall_activate": self.on_menu_selectall_activate,
            "on_menu_preferences_activate": self.on_menu_preferences_activate,

            # View menu
            "on_menu_statusbar_toggled": self.on_menu_statusbar_toggled,
            "on_menu_toolbar_toggled": self.on_menu_toolbar_toggled,
            "on_menu_rightpane_toggled": self.on_menu_rightpane_toggled,
            "on_menu_fullscreen_toggled": self.on_menu_fullscreen_toggled,

            # Search menu
            "on_menu_find_activate": self.on_menu_find_activate,
            "on_menu_findnext_activate": self.on_menu_findnext_activate,
            "on_menu_findprev_activate": self.on_menu_findprev_activate,

            # Document menu
            "on_menu_pdfcompile_activate": self.on_menu_pdfcompile_activate,
            "on_menu_compileopts_activate": self.on_menu_compileopts_activate,
            "on_menu_cleanup_activate": self.on_menu_cleanup_activate,
            "on_menu_runmakeindex_activate": self.on_menu_runmakeindex_activate,
            "on_menu_runbibtex_activate": self.on_menu_runbibtex_activate,
            "on_menu_docstat_activate": self.on_menu_docstat_activate,
            "on_menu_spelling_toggled": self.on_menu_spelling_toggled,
            "on_menu_snippets_toggled": self.on_menu_snippets_toggled,

            # Project menu
            "on_menu_project_activate": self.on_menu_project_activate,
            "on_menu_project_deselect": self.on_menu_project_deselect,
            "on_menu_projcreate_activate": self.on_menu_projcreate_activate,
            "on_menu_projopen_activate": self.on_menu_projopen_activate,
            "on_menu_projclose_activate": self.on_menu_projclose_activate,

            # Help menu
            "on_menu_guide_activate": self.on_menu_guide_activate,
            "on_menu_about_activate": self.on_menu_about_activate
        }
        
        self.builder.connect_signals(signals)

    # File Menu Handlers
    def on_menu_new_activate(self, widget, user_data=None):
        if not self.gui.rightpane.get_sensitive():
            self.gui_set_hastabs_sensitive(True)
        self.gummi.tabmanager.create_tab(0, None, None)

    def on_menu_template_activate(self, widget, user_data=None):
        self.gummi.templ.list_templates.clear()
        self.gummi.templ.setup()
        self.gummi.templ.templatewindow.set_visible(True)

    def on_menu_open_activate(self, widget, user_data=None):
        filename = self.get_open_filename(TYPE_LATEX)
        if filename:
            self.gui_open_file(filename)
            if self.gummi.active_editor:
                self.gummi.active_editor.view.grab_focus()

    # ... [Additional file menu handlers]

    # Edit Menu Handlers  
    def on_menu_edit_activate(self, widget, user_data=None):
        if not self.gummi.active_editor:
            return
        has_selection = self.gummi.active_editor.buffer.get_has_selection()
        self.menu_cut.set_sensitive(has_selection)
        self.menu_copy.set_sensitive(has_selection)

    def on_menu_cut_activate(self, widget, user_data=None):
        clipboard = Gdk.Display.get_default().get_clipboard()
        self.gummi.active_editor.buffer.cut_clipboard(clipboard, True)

    # ... [Additional edit menu handlers]

    # View Menu Handlers
    def on_menu_statusbar_toggled(self, widget, user_data=None):
        visible = widget.get_active()
        self.gui.statusbar.set_visible(visible)
        self.config_set_boolean("Interface", "statusbar", visible)

    # ... [Additional view menu handlers]

    # Document Menu Handlers
    def on_menu_docstat_activate(self, widget, user_data=None):
        """Handle document statistics activation"""
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

        if self.external_exists("texcount"):
            try:
                # Process document statistics
                tmpfile = f"{self.gummi.active_editor.fdname}.state"
                if not self.utils_copy_file(self.gummi.active_editor.workfile, tmpfile):
                    self.slog.error("Error copying file")
                    return

                results = self.process_texcount(tmpfile, terms_regex)
                if results:
                    self.update_stats_display(results)
                os.unlink(tmpfile)
                
            except Exception as err:
                self.slog.error(f"Error processing document statistics: {err}")
                return
        else:
            self.slog.error("The 'texcount' utility could not be found.")
            return

    # Project Menu Handlers
    def on_menu_project_activate(self, widget, user_data=None):
        if not self.gummi.active_editor:
            self.menu_projcreate.set_tooltip_text(
                _("This function requires an active document"))
            return

        if not self.gummi_project_active():
            self.menu_projopen.set_sensitive(True)
            if self.gummi.active_editor.filename is not None:
                self.menu_projcreate.set_sensitive(True)
            else:
                self.menu_projcreate.set_tooltip_text(
                    _("This function requires the current\n"
                      "active document to be saved. "))
        else:
            self.menu_projclose.set_sensitive(True)

    # Help Menu Handlers
    def on_menu_about_activate(self, widget, user_data=None):
        try:
            icon_file = os.path.join(GUMMI_DATA, "icons", "gummi.png")
            icon = GdkPixbuf.Pixbuf.new_from_file_at_size(icon_file, 80, 80)
        except Exception:
            icon = None

        about = Gtk.AboutDialog()
        about.set_transient_for(self.main_window)
        about.set_modal(True)
        about.set_authors(self.C_CREDITS_DEVELOPERS)
        about.set_program_name(self.C_PACKAGE_NAME)
        about.set_version(self.C_PACKAGE_VERSION)
        about.set_website(self.C_PACKAGE_URL)
        about.set_copyright(self.C_PACKAGE_COPYRIGHT)
        about.set_license(self.C_PACKAGE_LICENSE)
        about.set_logo(icon)
        about.set_comments(self.C_PACKAGE_COMMENTS)
        about.set_translator_credits(self.C_CREDITS_TRANSLATORS)
        about.set_documenters(self.C_CREDITS_DOCUMENTERS)
        
        about.show()

    # Utility methods
    def process_texcount(self, tmpfile: str, terms_regex: list) -> list:
        """Process texcount output for the given file"""
        cmd = f"texcount '{tmpfile}'"
        result = self.utils_popen_r(cmd)
        
        regexs = []
        results = [None] * TEXCOUNT_OUTPUT_LINES
        
        try:
            for i in range(TEXCOUNT_OUTPUT_LINES):
                regexs.append(re.compile(terms_regex[i]))
                
            for i in range(TEXCOUNT_OUTPUT_LINES):
                match = regexs[i].search(result[1])
                if match:
                    results[i] = match.group(1) if match.group(1) else "N/A"
                    
            return results
            
        except re.error as err:
            self.slog.error(f"Regex compile error: {err}")
            return None

    def update_stats_display(self, results: list):
        """Update statistics display with results"""
        items = ["stats_words", "stats_head", "stats_float",
                "stats_nrhead", "stats_nrfloat", "stats_nrmath"]
                
        for j, item in enumerate(items):
            label = self.builder.get_object(item)
            if label:
                label.set_text(results[j])
                
        filename_label = self.builder.get_object("stats_filename")
        if filename_label:
            filename_label.set_text(
                self.tabmanagergui.get_labeltext(self.gummi.active_tab.page))
            
        self.docstatswindow.show()

def create_menu(builder, main_window):
    """Create and return a new MenuGui instance"""
    return MenuGui(builder, main_window)
