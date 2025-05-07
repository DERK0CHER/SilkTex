#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
@file   tabmanager.py
@brief  Tab manager for Gummi using GTK4 and GtkSourceView 5

Copyright (C) 2009 Gummi Developers
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
from gi.repository import Gtk, GtkSource, GLib
from enum import Enum, auto
from typing import List, Optional, Callable, Any

# These would be imported from other modules in the application
from environment import gummi_new_environment
from iofunctions import (has_swapfile, load_default_text, 
                         load_file, add_to_recent_list)
import logging

# Global variables that would typically be handled differently in a Python app
# but kept similar to match the C style
gummi = None
gui = None

class OpenAct(Enum):
    """Actions for opening tabs"""
    A_NONE = auto()
    A_DEFAULT = auto()
    A_LOAD = auto()
    A_LOAD_OPT = auto()

class TabContext:
    """Context for each tab holding editor reference and page information"""
    def __init__(self):
        self.editor = None
        self.page = None  # Would store page-specific data

class TabManager:
    """Manages tabs for the editor"""
    
    def __init__(self):
        """Initialize the tab manager"""
        self.tabs: List[TabContext] = []
        self.active_editor = None
        self.active_tab = None
        self.tab_notebook = None  # Reference to GTK notebook widget

    def set_tab_notebook(self, notebook):
        """Set the GTK notebook widget reference"""
        self.tab_notebook = notebook

    def foreach_editor(self, func: Callable, user_data: Any) -> None:
        """Apply a function to each editor"""
        tabs = self.tabs.copy()  # Create a copy to avoid modification issues
        for tab in tabs:
            func(tab.editor, user_data)

    def _current_tab_replaceable(self, act: OpenAct) -> bool:
        """Check if the current tab can be replaced"""
        if act in (OpenAct.A_LOAD, OpenAct.A_LOAD_OPT):
            if self.active_editor and not self.active_editor.filename:
                if not self.active_editor.buffer_changed():
                    return True
        return False

    def get_tabname(self, tc: TabContext) -> str:
        """Get the name to display on the tab"""
        if tc.editor.filename:
            filetext = os.path.basename(tc.editor.filename)
        else:
            filetext = f"Unsaved Document {tc.page.unsavednr}"
        
        modified = tc.editor.buffer_changed()
        labeltext = f"{'*' if modified else ''}{filetext}"
        
        return labeltext

    def remove_tab(self, tab: TabContext) -> int:
        """Remove a tab and return number of remaining tabs"""
        position = self.tabs.index(tab)
        total = self.get_n_pages()
        
        if total == 0:
            return 0
        
        self.tabs.remove(tab)
        self.set_active_tab(total - 2)  # Select previous tab
        
        tab.editor.destroy()
        self.tab_notebook.remove_page(position)
        
        # Python handles memory management automatically
        return total - 1  # Return number of remaining tabs

    def set_active_tab(self, position: int) -> None:
        """Set the active tab based on position"""
        if position == -1:
            self.active_tab = None
            self.active_editor = None
        else:
            try:
                self.active_tab = self.tabs[position]
                self.active_editor = self.tabs[position].editor
            except IndexError:
                logging.error(f"Invalid tab position: {position}")
                self.active_tab = None
                self.active_editor = None

    def create_tab(self, act: OpenAct, filename: Optional[str], opt: Optional[str]) -> None:
        """Create a new tab with the specified parameters"""
        editor = gummi_new_environment(filename)
        pos = 0
        
        if self._current_tab_replaceable(act):
            pos = self._replace_page(self.active_tab, editor)
        else:
            tc = TabContext()
            tc.editor = editor
            self.tabs.append(tc)
            pos = self._create_page(tc, tc.editor)
            self._set_current_page(pos)
        
        self.set_active_tab(pos)
        
        if has_swapfile(filename):
            gui.recovery_mode_enable(self.active_tab, filename)
            # Signal handles tabmanager_set_content in this case
        else:
            self.set_content(act, filename, opt)
        
        gui.set_filename_display(self.active_tab, True, True)
        add_to_recent_list(editor.filename)
        
        gui.previewgui.reset()

    def set_content(self, act: OpenAct, filename: Optional[str], opt: Optional[str]) -> None:
        """Load the appropriate content in the editor"""
        if act == OpenAct.A_NONE:
            pass
        elif act == OpenAct.A_DEFAULT:
            load_default_text(False)
        elif act == OpenAct.A_LOAD:
            load_file(gummi.io, filename)
        elif act == OpenAct.A_LOAD_OPT:
            load_file(gummi.io, opt)
        else:
            logging.fatal("Invalid OpenAct value")

    def update_tab(self, filename: str) -> None:
        """Update tab when document is saved"""
        gui.set_filename_display(self.active_tab, True, True)
        self.active_tab.editor.fileinfo_update(filename)
        
        # Add full filepath to recent list
        add_to_recent_list(self.active_tab.editor.filename)
        
        logging.info(f"Environment updated for {self.active_tab.editor.filename}")
        gui.previewgui.reset()

    def has_tabs(self) -> bool:
        """Check if there are any tabs"""
        if len(self.tabs) == 0:
            if self.active_editor is not None:
                logging.error("Something went terribly wrong in has_tabs")
            return False
        return True

    def check_exists(self, filename: str) -> bool:
        """Check if a file is already open in any tab"""
        if not filename:
            return False
            
        for tab in self.tabs:
            if tab.editor.filename == filename:
                return True
        return False
        
    # Internal methods for GTK4 notebook management
    def _create_page(self, tc: TabContext, editor) -> int:
        """Create a new page in the notebook"""
        # Create tab content
        scrolled_window = Gtk.ScrolledWindow()
        scrolled_window.set_child(editor.source_view)
        scrolled_window.set_vexpand(True)
        scrolled_window.set_hexpand(True)
        
        # Create tab label
        label_text = self.get_tabname(tc)
        label = Gtk.Label(label=label_text)
        
        # Create tab with close button
        tab_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=5)
        tab_box.append(label)
        
        close_button = Gtk.Button()
        close_button.set_icon_name("window-close-symbolic")
        close_button.add_css_class("flat")
        close_button.connect("clicked", lambda btn: self.remove_tab(tc))
        tab_box.append(close_button)
        
        # Add page to notebook
        page_num = self.tab_notebook.append_page(scrolled_window, tab_box)
        tc.page = self.tab_notebook.get_page(scrolled_window)
        
        # Setup page data
        if not hasattr(tc.page, 'unsavednr'):
            tc.page.unsavednr = len([t for t in self.tabs if not t.editor.filename])
        
        # Show all widgets
        scrolled_window.show()
        tab_box.show()
        
        return page_num
        
    def _replace_page(self, tab: TabContext, editor) -> int:
        """Replace the contents of a page with a new editor"""
        pos = self.tabs.index(tab)
        
        # Remove the old editor and add the new one
        page = self.tab_notebook.get_nth_page(pos)
        if isinstance(page, Gtk.ScrolledWindow):
            page.set_child(None)  # Remove old sourceview
            page.set_child(editor.source_view)  # Add new sourceview
        
        # Update the tab context
        tab.editor = editor
        
        return pos
        
    def _set_current_page(self, page_num: int) -> None:
        """Set the current page in the notebook"""
        self.tab_notebook.set_current_page(page_num)
        
    def get_n_pages(self) -> int:
        """Get the number of pages in the notebook"""
        return self.tab_notebook.get_n_pages()


def tabmanager_init() -> TabManager:
    """Initialize tab manager (module-level function to match C style)"""
    return TabManager()