#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Search functionality GUI for Gummi.

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

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GLib

from editor import Editor
from environment import Environment
from utils import Utils
from gui.gui_main import GummiGui


class SearchGui:
    """Search GUI handling class for Gummi."""
    
    def __init__(self, builder, gummi, gui):
        """Initialize the search GUI.
        
        Args:
            builder: Gtk.Builder instance with the UI definitions
            gummi: Reference to the main Gummi instance
            gui: Reference to the main GUI instance
        """
        self.gummi = gummi
        self.gui = gui
        
        # Get widgets from builder
        self.searchwindow = builder.get_object("searchwindow")
        self.searchentry = builder.get_object("searchentry")
        self.replaceentry = builder.get_object("replaceentry")
        
        # Initialize search options
        self.matchcase = False
        self.backwards = False
        self.wholeword = False
        self.prev_search = None
        self.prev_replace = None
        
        # Connect signals
        self.searchentry.connect("changed", self.on_searchgui_text_changed)
    
    def main(self):
        """Show the search window and prepare it for use."""
        # Set previous search terms if available
        self.searchentry.set_text(self.prev_search if self.prev_search else "")
        self.replaceentry.set_text(self.prev_replace if self.prev_replace else "")
        
        # Give focus to search entry
        self.searchentry.grab_focus()
        
        # Show the search window
        self.searchwindow.set_visible(True)
        
        # Set the search window as the parent for logging
        from utils import slog_set_gui_parent
        slog_set_gui_parent(self.searchwindow)
    
    def on_toggle_matchcase_toggled(self, widget, user_data=None):
        """Handle toggling of the match case option.
        
        Args:
            widget: The toggle button widget
            user_data: Additional data (unused)
        """
        self.matchcase = widget.get_active()
        self.gui.active_editor.replace_activated = False
    
    def on_toggle_wholeword_toggled(self, widget, user_data=None):
        """Handle toggling of the whole word option.
        
        Args:
            widget: The toggle button widget
            user_data: Additional data (unused)
        """
        self.wholeword = widget.get_active()
        self.gui.active_editor.replace_activated = False
    
    def on_toggle_backwards_toggled(self, widget, user_data=None):
        """Handle toggling of the search backwards option.
        
        Args:
            widget: The toggle button widget
            user_data: Additional data (unused)
        """
        self.backwards = widget.get_active()
        self.gui.active_editor.replace_activated = False
    
    def on_searchgui_text_changed(self, editable, user_data=None):
        """Handle changes to the search text.
        
        Args:
            editable: The editable widget that changed
            user_data: Additional data (unused)
        """
        self.gui.active_editor.replace_activated = False
    
    def on_button_searchwindow_close_clicked(self, widget, user_data=None):
        """Handle clicking of the close button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
            
        Returns:
            True to stop further signal processing
        """
        # Save current search terms for later use
        self.prev_search = self.searchentry.get_text()
        self.prev_replace = self.replaceentry.get_text()
        
        # Hide the search window
        self.searchwindow.set_visible(False)
        
        # Set the main window as the parent for logging
        from utils import slog_set_gui_parent
        slog_set_gui_parent(self.gui.mainwindow)
        
        return True
    
    def on_button_searchwindow_find_clicked(self, widget, user_data=None):
        """Handle clicking of the find button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        # Start the search in the active editor
        self.gui.active_editor.start_search(
            self.searchentry.get_text(),
            self.backwards,
            self.wholeword,
            self.matchcase
        )
    
    def on_button_searchwindow_replace_next_clicked(self, widget, user_data=None):
        """Handle clicking of the replace next button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        # Start the replace next operation in the active editor
        self.gui.active_editor.start_replace_next(
            self.searchentry.get_text(),
            self.replaceentry.get_text(),
            self.backwards,
            self.wholeword,
            self.matchcase
        )
    
    def on_button_searchwindow_replace_all_clicked(self, widget, user_data=None):
        """Handle clicking of the replace all button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        # Start the replace all operation in the active editor
        self.gui.active_editor.start_replace_all(
            self.searchentry.get_text(),
            self.replaceentry.get_text(),
            self.backwards,
            self.wholeword,
            self.matchcase
        )


def searchgui_init(builder, gummi, gui):
    """Initialize and return a new SearchGui instance.
    
    Args:
        builder: Gtk.Builder instance with the UI definitions
        gummi: Reference to the main Gummi instance
        gui: Reference to the main GUI instance
        
    Returns:
        A new SearchGui instance
    """
    if not isinstance(builder, Gtk.Builder):
        return None
    
    return SearchGui(builder, gummi, gui)