#!/usr/bin/env python3
"""
template.py - Template management for Gummi

Copyright (C) 2025 Gummi Developers
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
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Adw, Gio, GLib, GtkSource

from . import utils
from . import config
from . import environment
from . import constants

# Constants
DIR_PERMS = 0o755


class Template:
    """Class to manage LaTeX templates in Gummi"""

    def __init__(self, builder):
        """Initialize the template manager with the GTK builder object"""
        if not isinstance(builder, Gtk.Builder):
            raise TypeError("Expected a Gtk.Builder instance")
            
        self.builder = builder
        
        # Get widgets from the builder
        self.templatewindow = builder.get_object("templatewindow")
        self.templateview = builder.get_object("template_treeview")
        self.template_label = builder.get_object("template_label")
        self.template_add = builder.get_object("template_add")
        self.template_remove = builder.get_object("template_remove")
        self.template_open = builder.get_object("template_open")
        
        # Create store and set up columns - GTK4 way
        self.list_templates = Gtk.ListStore.new([str, str])  # filename, filepath
        self.templateview.set_model(self.list_templates)
        
        # Create column
        renderer = Gtk.CellRendererText()
        column = Gtk.ColumnView.new_column("Template")
        column.set_expand(True)
        column.set_cell_data_func(renderer, self._set_cell_data)
        column.set_sorter(Gtk.Sorter.new_for_column(self.list_templates, 0))
        
        self.template_render = renderer
        self.template_col = column
        
        # Connect signals
        self.template_render.connect("edited", self._on_cell_edited)
        self.templateview.connect("activate", self._on_row_activated)
        
        # Store references to the signals for later
        self.selection_changed_id = self.templateview.get_selection().connect(
            "changed", self._on_selection_changed)
            
    def _set_cell_data(self, column, cell, model, iter, data):
        """Set cell renderer data from the model"""
        filename = model.get_value(iter, 0)
        cell.set_property("text", filename)
        
    def setup(self):
        """Set up the template manager by loading templates from disk"""
        # Create config directory if it doesn't exist
        dirpath = os.path.join(GLib.get_user_config_dir(), "gummi", "templates")
        
        try:
            if not os.path.exists(dirpath):
                utils.slog("INFO", "unable to read template directory, creating new..")
                os.makedirs(dirpath, mode=DIR_PERMS, exist_ok=True)
                return
        except Exception as e:
            utils.slog("ERROR", f"Failed to create template directory: {e}")
            return
            
        # Load templates
        try:
            for filename in os.listdir(dirpath):
                filepath = os.path.join(dirpath, filename)
                iter = self.list_templates.append()
                self.list_templates.set(iter, 0, filename, 1, filepath)
        except Exception as e:
            utils.slog("ERROR", f"Failed to read template directory: {e}")
            
        # Disable add button when no tabs are open
        if not environment.tabmanager_has_tabs():
            self.template_add.set_sensitive(False)
            
        # Disable open button initially
        self.template_open.set_sensitive(False)
        
    def get_selected_path(self):
        """Get the filepath of the selected template"""
        selection = self.templateview.get_selection()
        model, iter = selection.get_selected()
        
        if iter is not None:
            return model.get_value(iter, 1)
        return None
        
    def add_new_entry(self):
        """Add a new template entry and start editing it"""
        self.template_label.set_text("")
        
        # Add new row
        iter = self.list_templates.append()
        
        # Make cell editable and disable buttons
        self.template_render.set_property("editable", True)
        self.template_add.set_sensitive(False)
        self.template_remove.set_sensitive(False)
        self.template_open.set_sensitive(False)
        
        # Start editing the cell
        path = self.list_templates.get_path(iter)
        self.templateview.set_cursor(path, self.template_col, True)
        
    def remove_entry(self):
        """Remove the selected template entry and its file"""
        selection = self.templateview.get_selection()
        model, iter = selection.get_selected()
        
        if iter is not None:
            filepath = model.get_value(iter, 1)
            model.remove(iter)
            
            # Remove file if it exists
            if os.path.isfile(filepath):
                try:
                    os.remove(filepath)
                except Exception as e:
                    utils.slog("ERROR", f"Failed to remove template file: {e}")
                    
        self.template_open.set_sensitive(False)
        
    def create_file(self, filename, text):
        """Create a new template file with the given name and content"""
        filepath = os.path.join(constants.C_GUMMI_TEMPLATEDIR, filename)
        
        if not filename:
            self.template_label.set_text("filename cannot be empty")
            self.remove_entry()
        elif os.path.exists(filepath):
            self.template_label.set_text("filename already exists")
            self.remove_entry()
        else:
            try:
                with open(filepath, 'w') as f:
                    f.write(text)
            except Exception as e:
                utils.slog("ERROR", f"Failed to create template file: {e}")
                
        # Reset UI state
        self.template_render.set_property("editable", False)
        self.template_add.set_sensitive(True)
        self.template_remove.set_sensitive(True)
        self.template_open.set_sensitive(True)
        
    def _on_cell_edited(self, renderer, path, new_text):
        """Handle cell editing completion"""
        # Get the current editor content
        current_content = environment.get_active_editor_content()
        self.create_file(new_text, current_content)
        
        # Update the model with the new filename and filepath
        iter = self.list_templates.get_iter_from_string(path)
        filepath = os.path.join(constants.C_GUMMI_TEMPLATEDIR, new_text)
        self.list_templates.set(iter, 0, new_text, 1, filepath)
        
    def _on_selection_changed(self, selection):
        """Handle selection change in the template view"""
        model, iter = selection.get_selected()
        self.template_open.set_sensitive(iter is not None)
        
    def _on_row_activated(self, treeview, path, column):
        """Handle double-click on a template"""
        # This would typically open the template
        filepath = self.get_selected_path()
        if filepath:
            environment.open_file(filepath)
            
    def cleanup(self):
        """Free resources used by template manager"""
        # Disconnect signals
        selection = self.templateview.get_selection()
        selection.disconnect(self.selection_changed_id)