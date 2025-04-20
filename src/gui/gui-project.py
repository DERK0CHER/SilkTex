#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Project GUI module for Gummi.

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
from gi.repository import Gtk, GLib, Gio, GdkPixbuf

from environment import Environment
from gui.gui_main import GummiGui
from gui.gui_tabmanager import tabmanagergui_set_current_page, tablabel_set_bold_text
from project import project_list_files, project_add_document, project_remove_document
from utils import get_open_filename, statusbar_set_message, slog, LogLevel


class ProjectGui:
    """Project GUI handling class for Gummi."""
    
    def __init__(self, builder, gummi, gui):
        """Initialize the project GUI.
        
        Args:
            builder: Gtk.Builder instance with the UI definitions
            gummi: Reference to the main Gummi instance
            gui: Reference to the main GUI instance
        """
        self.gummi = gummi
        self.gui = gui
        
        # Get widgets from builder
        self.proj_name = builder.get_object("proj_name")
        self.proj_path = builder.get_object("proj_path")
        self.proj_nroffiles = builder.get_object("proj_nroffiles")
        
        self.proj_addbutton = builder.get_object("proj_addbutton")
        self.proj_rembutton = builder.get_object("proj_rembutton")
        
        self.list_projfiles = builder.get_object("list_projfiles")
        self.proj_treeview = builder.get_object("proj_treeview")
    
    def set_rootfile(self, position):
        """Set the root file for the project.
        
        Args:
            position: Position of the root file in the tab list
        """
        tabmanagergui_set_current_page(position)
        tablabel_set_bold_text(self.gummi.active_tab.page)
    
    def list_projfiles(self, active_proj):
        """List all project files in the treeview.
        
        Args:
            active_proj: Path to the active project file
            
        Returns:
            Number of files in the project, or -1 on error
        """
        store = self.list_projfiles
        store.clear()
        
        try:
            with open(active_proj, 'r') as f:
                content = f.read()
        except Exception as err:
            slog(LogLevel.ERROR, f"{err}")
            return -1
        
        files = project_list_files(content)
        amount = len(files)
        
        for i, file_path in enumerate(files):
            pic = None
            name = os.path.basename(file_path)
            path = os.path.dirname(file_path)
            
            # 0=ROOT, 1=ERROR
            if i == 0:
                pic = self.get_status_pixbuf(0)
            if not os.path.exists(file_path):
                pic = self.get_status_pixbuf(1)
            
            iter_val = store.append()
            store.set(iter_val, 
                     0, pic,  # Icon
                     1, name, # Filename
                     2, path, # Directory
                     3, file_path) # Full path
        
        return amount
    
    def get_status_pixbuf(self, status):
        """Get a pixbuf for the status icon.
        
        Args:
            status: Status code (0=ROOT, 1=ERROR)
            
        Returns:
            A GdkPixbuf for the status
        """
        # In GTK4, we need to use Gtk.IconTheme.get_for_display()
        # instead of get_default()
        display = Gdk.Display.get_default()
        theme = Gtk.IconTheme.get_for_display(display)
        
        # GTK_ICON_SIZE constants are no longer used in GTK4
        # We specify an explicit size instead (16 is equivalent to MENU size)
        icon_size = 16
        
        try:
            if status == 0:
                return theme.lookup_icon("go-home", icon_size, 0).load_texture()
            elif status == 1:
                return theme.lookup_icon("process-stop", icon_size, 0).load_texture()
        except GLib.Error:
            # Fallback handling
            pass
        
        return None
    
    def enable(self, project):
        """Enable the project GUI.
        
        Args:
            project: The active project
        """
        proj_basename = os.path.basename(project.projfile)
        proj_rootpath = os.path.dirname(project.rootfile)
        
        self.proj_name.set_text(proj_basename)
        self.proj_path.set_text(proj_rootpath)
        self.proj_nroffiles.set_text(str(project.nroffiles))
        
        # For visible information when window is shrunk, see #439
        self.proj_name.set_tooltip_text(proj_basename)
        self.proj_path.set_tooltip_text(proj_rootpath)
        
        self.proj_addbutton.set_sensitive(True)
        self.proj_rembutton.set_sensitive(True)
        
        tablabel_set_bold_text(self.gummi.active_tab.page)
    
    def disable(self, project):
        """Disable the project GUI.
        
        Args:
            project: The active project
        """
        self.list_projfiles.clear()
        
        self.proj_name.set_text("")
        self.proj_path.set_text("")
        self.proj_nroffiles.set_text("")
        
        self.proj_name.set_tooltip_text("")
        self.proj_path.set_tooltip_text("")
        
        self.proj_addbutton.set_sensitive(False)
        self.proj_rembutton.set_sensitive(False)
    
    def on_projfile_add_clicked(self, widget, user_data=None):
        """Handle clicking of the add project file button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        selected = get_open_filename("latex")  # TYPE_LATEX converted to string
        
        if selected:
            if project_add_document(self.gummi.project.projfile, selected):
                amount = self.list_projfiles(self.gummi.project.projfile)
                self.proj_nroffiles.set_text(str(amount))
                self.gui.open_file(selected)
            else:
                statusbar_set_message("Error adding document to the project..")
    
    def on_projfile_rem_clicked(self, widget, user_data=None):
        """Handle clicking of the remove project file button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        model = self.list_projfiles
        selection = self.proj_treeview.get_selection()
        
        # Get the selected row
        model, iter_val = selection.get_selected()
        
        if iter_val:
            value = model.get_value(iter_val, 3)  # Get the full file path (column 3)
            
            if project_remove_document(self.gummi.project.projfile, value):
                amount = self.list_projfiles(self.gummi.project.projfile)
                self.proj_nroffiles.set_text(str(amount))


def projectgui_init(builder, gummi, gui):
    """Initialize and return a new ProjectGui instance.
    
    Args:
        builder: Gtk.Builder instance with the UI definitions
        gummi: Reference to the main Gummi instance
        gui: Reference to the main GUI instance
        
    Returns:
        A new ProjectGui instance
    """
    if not isinstance(builder, Gtk.Builder):
        return None
    
    return ProjectGui(builder, gummi, gui)


# Wrapper functions to maintain compatibility with original C API

def projectgui_set_rootfile(position):
    """Set the root file for the project (wrapper function).
    
    Args:
        position: Position of the root file in the tab list
    """
    from gui.gui_main import gui  # Import here to avoid circular imports
    gui.projectgui.set_rootfile(position)

def projectgui_list_projfiles(active_proj):
    """List all project files (wrapper function).
    
    Args:
        active_proj: Path to the active project file
        
    Returns:
        Number of files in the project, or -1 on error
    """
    from gui.gui_main import gui
    return gui.projectgui.list_projfiles(active_proj)

def projectgui_get_status_pixbuf(status):
    """Get a pixbuf for status (wrapper function).
    
    Args:
        status: Status code
        
    Returns:
        A GdkPixbuf for the status
    """
    from gui.gui_main import gui
    return gui.projectgui.get_status_pixbuf(status)

def projectgui_enable(project, project_gui=None):
    """Enable the project GUI (wrapper function).
    
    Args:
        project: The project to enable
        project_gui: Optional project GUI instance (unused in wrapper)
    """
    from gui.gui_main import gui
    gui.projectgui.enable(project)

def projectgui_disable(project, project_gui=None):
    """Disable the project GUI (wrapper function).
    
    Args:
        project: The project to disable
        project_gui: Optional project GUI instance (unused in wrapper)
    """
    from gui.gui_main import gui
    gui.projectgui.disable(project)