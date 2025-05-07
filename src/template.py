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
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Template Manager module

This module handles template management for LaTeX documents, including
selection, loading, and applying templates.
"""

import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gio, GLib

class TemplateManager:
    """
    Template manager for LaTeX documents.
    
    This class handles loading, selecting, and applying LaTeX templates.
    """
    
    def __init__(self, window):
        """Initialize the template manager."""
        self.window = window
        self.templates = []
        self.dialog = None
        
        # Load available templates
        self.load_templates()
    
    def load_templates(self):
        """Load available templates from the templates directory."""
        self.templates = []
        
        # Define template directories
        template_dirs = [
            os.path.join(os.path.dirname(__file__), '..', 'data', 'templates'),
            os.path.join(os.path.dirname(__file__), 'data', 'templates'),
            os.path.join(GLib.get_user_data_dir(), 'silktex', 'templates'),
            '/usr/share/silktex/templates',
            '/usr/local/share/silktex/templates'
        ]
        
        # Built-in templates
        basic_template = {
            'name': 'Basic Article',
            'description': 'A simple article template',
            'content': (
                '\\documentclass{article}\n\n'
                '\\title{Document Title}\n'
                '\\author{Author Name}\n'
                '\\date{\\today}\n\n'
                '\\begin{document}\n\n'
                '\\maketitle\n\n'
                '\\section{Introduction}\n'
                'Your introduction goes here.\n\n'
                '\\section{Content}\n'
                'Your content goes here.\n\n'
                '\\section{Conclusion}\n'
                'Your conclusion goes here.\n\n'
                '\\end{document}\n'
            )
        }
        self.templates.append(basic_template)
        
        report_template = {
            'name': 'Report',
            'description': 'A template for longer reports',
            'content': (
                '\\documentclass{report}\n\n'
                '\\title{Report Title}\n'
                '\\author{Author Name}\n'
                '\\date{\\today}\n\n'
                '\\begin{document}\n\n'
                '\\maketitle\n'
                '\\tableofcontents\n\n'
                '\\chapter{Introduction}\n'
                'Your introduction goes here.\n\n'
                '\\chapter{Background}\n'
                'Background information goes here.\n\n'
                '\\chapter{Methodology}\n'
                'Your methodology goes here.\n\n'
                '\\chapter{Results}\n'
                'Your results go here.\n\n'
                '\\chapter{Conclusion}\n'
                'Your conclusion goes here.\n\n'
                '\\end{document}\n'
            )
        }
        self.templates.append(report_template)
        
        beamer_template = {
            'name': 'Beamer Presentation',
            'description': 'A template for presentations',
            'content': (
                '\\documentclass{beamer}\n\n'
                '\\usetheme{Madrid}\n'
                '\\usecolortheme{default}\n\n'
                '\\title{Presentation Title}\n'
                '\\author{Author Name}\n'
                '\\date{\\today}\n\n'
                '\\begin{document}\n\n'
                '\\begin{frame}\n'
                '\\titlepage\n'
                '\\end{frame}\n\n'
                '\\begin{frame}{Outline}\n'
                '\\tableofcontents\n'
                '\\end{frame}\n\n'
                '\\section{First Section}\n\n'
                '\\begin{frame}{First Section}\n'
                'Content for the first section\n'
                '\\end{frame}\n\n'
                '\\section{Second Section}\n\n'
                '\\begin{frame}{Second Section}\n'
                'Content for the second section\n'
                '\\end{frame}\n\n'
                '\\begin{frame}{Summary}\n'
                '\\begin{itemize}\n'
                '\\item First point\n'
                '\\item Second point\n'
                '\\item Third point\n'
                '\\end{itemize}\n'
                '\\end{frame}\n\n'
                '\\end{document}\n'
            )
        }
        self.templates.append(beamer_template)
        
        # Load templates from files
        for template_dir in template_dirs:
            if os.path.exists(template_dir) and os.path.isdir(template_dir):
                for filename in os.listdir(template_dir):
                    if filename.endswith('.tex'):
                        filepath = os.path.join(template_dir, filename)
                        try:
                            with open(filepath, 'r', encoding='utf-8') as f:
                                content = f.read()
                            
                            # Extract name from filename
                            name = filename[:-4].replace('_', ' ').title()
                            
                            # Try to extract title from content
                            title_match = re.search(r'\\title\{(.*?)\}', content)
                            if title_match:
                                name = title_match.group(1)
                            
                            template = {
                                'name': name,
                                'description': f'Template from {filepath}',
                                'content': content,
                                'filepath': filepath
                            }
                            self.templates.append(template)
                        except Exception as e:
                            print(f"Error loading template {filepath}: {e}")
    
    def show_template_selector(self):
        """Show a dialog for selecting a template."""
        # Create the dialog
        self.dialog = Adw.Dialog()
        self.dialog.set_title("Select Template")
        self.dialog.set_modal(True)
        self.dialog.set_transient_for(self.window)
        
        # Create a vertical box for content
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        
        # Create a header for the dialog
        header = Adw.HeaderBar()
        cancel_button = Gtk.Button.new_with_label("Cancel")
        cancel_button.connect("clicked", self._on_cancel_clicked)
        header.pack_start(cancel_button)
        content_box.append(header)
        
        # Create a list box for templates
        scrolled_window = Gtk.ScrolledWindow()
        scrolled_window.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scrolled_window.set_min_content_height(400)
        scrolled_window.set_min_content_width(400)
        
        self.template_list = Gtk.ListBox()
        self.template_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.template_list.set_show_separators(True)
        self.template_list.connect("row-activated", self._on_template_selected)
        
        # Add templates to the list
        for i, template in enumerate(self.templates):
            row = Adw.ActionRow()
            row.set_title(template['name'])
            row.set_subtitle(template['description'])
            
            # Store template index in the row
            row.set_data("template-index", i)
            
            self.template_list.append(row)
        
        scrolled_window.set_child(self.template_list)
        content_box.append(scrolled_window)
        
        # Set content and present dialog
        self.dialog.set_content(content_box)
        self.dialog.present()
    
    def _on_cancel_clicked(self, button):
        """Handle cancel button click."""
        self.dialog.close()
    
    def _on_template_selected(self, list_box, row):
        """Handle template selection."""
        if row is None:
            return
        
        # Get the selected template index
        index = row.get_data("template-index")
        if index is not None and 0 <= index < len(self.templates):
            template = self.templates[index]
            
            # Check if current document has unsaved changes
            if self.window.editor.is_modified():
                # Ask user if they want to discard changes
                confirm_dialog = Adw.MessageDialog.new(
                    self.window,
                    "Discard Changes?",
                    "The current document has unsaved changes. Applying a template will replace the current content."
                )
                confirm_dialog.add_response("cancel", "Cancel")
                confirm_dialog.add_response("discard", "Discard Changes")
                confirm_dialog.set_default_response("cancel")
                confirm_dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
                
                confirm_dialog.connect("response", self._on_confirm_response, template)
                confirm_dialog.present()
            else:
                # Apply template directly
                self._apply_template(template)
            
            # Close the template selector dialog
            self.dialog.close()
    
    def _on_confirm_response(self, dialog, response, template):
        """Handle response from the confirm dialog."""
        if response == "discard":
            self._apply_template(template)
        
        dialog.destroy()
    
    def _apply_template(self, template):
        """Apply the selected template.
        
        Args:
            template: The template to apply
        """
        # Set the template content as the document content
        self.window.editor.buffer.set_text(template['content'])
        self.window.editor.buffer.set_modified(True)
        
        # Update window title
        self.window.update_title()
        
        # Show a notification
        self.window.update_status(f"Applied template: {template['name']}")
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