#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Import GUI module for Gummi.

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
from gi.repository import Gtk

from editor import editor_get_current_iter, editor_insert_package
from utils import utils_path_exists, utils_path_to_relative
from importer import importer_generate_table, importer_generate_image, importer_generate_matrix

# Global variables (will be set in main application)
gummi = None
gui = None
g_active_editor = None
g_e_buffer = None
g_importgui = None

# Constants
TYPE_LATEX = 0
TYPE_LATEX_SAVEAS = 1
TYPE_PDF = 2
TYPE_IMAGE = 3
TYPE_BIBLIO = 4
TYPE_PROJECT = 5


class ImportGui:
    """Import GUI handling class for Gummi."""
    
    def __init__(self, builder):
        """Initialize the Import GUI.
        
        Args:
            builder: Gtk.Builder instance with the UI definitions
        """
        global g_importgui
        
        # Get widgets from builder
        self.import_panel = builder.get_object("import_panel")
        self.image_pane = builder.get_object("imp_pane_image")
        self.table_pane = builder.get_object("imp_pane_table")
        self.matrix_pane = builder.get_object("imp_pane_matrix")
        self.biblio_pane = builder.get_object("imp_pane_biblio")
        
        self.image_file = builder.get_object("image_file")
        self.image_caption = builder.get_object("image_caption")
        self.image_label = builder.get_object("image_label")
        self.image_scale = builder.get_object("image_scale")
        self.scaler = builder.get_object("image_scaler")
        
        self.table_comboalign = builder.get_object("table_comboalign")
        self.table_comboborder = builder.get_object("table_comboborder")
        self.table_rows = builder.get_object("table_rows")
        self.table_cols = builder.get_object("table_cols")
        
        self.matrix_rows = builder.get_object("matrix_rows")
        self.matrix_cols = builder.get_object("matrix_cols")
        self.matrix_combobracket = builder.get_object("matrix_combobracket")
        
        self.biblio_file = builder.get_object("biblio_file")
        
        # Set default values
        self.table_cols.set_value(3)
        self.table_rows.set_value(3)
        self.matrix_cols.set_value(3)
        self.matrix_rows.set_value(3)
        
        # Set global reference
        g_importgui = self
    
    def remove_all_panels(self):
        """Remove all panels from the import panel."""
        # GTK4 way: directly set child to None
        if self.import_panel.get_child():
            self.import_panel.set_child(None)
    
    def on_imp_panel_image_clicked(self, widget, user_data=None):
        """Handle click on the image import panel button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        self.remove_all_panels()
        self.import_panel.set_child(self.image_pane)
    
    def on_imp_panel_table_clicked(self, widget, user_data=None):
        """Handle click on the table import panel button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        self.remove_all_panels()
        self.import_panel.set_child(self.table_pane)
    
    def on_imp_panel_matrix_clicked(self, widget, user_data=None):
        """Handle click on the matrix import panel button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        self.remove_all_panels()
        self.import_panel.set_child(self.matrix_pane)
    
    def on_imp_panel_biblio_clicked(self, widget, user_data=None):
        """Handle click on the bibliography import panel button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        self.remove_all_panels()
        self.import_panel.set_child(self.biblio_pane)
    
    def on_imp_minimize_clicked(self, widget, user_data=None):
        """Handle click on the minimize button.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        self.remove_all_panels()
    
    def on_import_table_apply_clicked(self, widget, user_data=None):
        """Handle click on the apply button for table import.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        # Get current settings
        rows = int(self.table_rows.get_value())
        cols = int(self.table_cols.get_value())
        border = self.table_comboborder.get_active()
        align = self.table_comboalign.get_active()
        
        # Generate table LaTeX code
        text = importer_generate_table(rows, cols, border, align)
        
        # Insert at current position
        current = Gtk.TextIter()
        editor_get_current_iter(g_active_editor, current)
        
        # Begin/end user action for undo
        g_e_buffer.begin_user_action()
        g_e_buffer.insert(current, text, len(text))
        g_e_buffer.end_user_action()
        g_e_buffer.set_modified(True)
        
        # Close the panel
        self.remove_all_panels()
    
    def on_import_image_apply_clicked(self, widget, user_data=None):
        """Handle click on the apply button for image import.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        # Get current settings
        imagefile = self.image_file.get_text()
        caption = self.image_caption.get_text()
        label = self.image_label.get_text()
        scale = self.scaler.get_value()
        
        root_path = None
        relative_path = None
        
        if imagefile and len(imagefile) > 0:
            if not utils_path_exists(imagefile):
                from logger import slog, L_G_ERROR
                slog(L_G_ERROR, _("%s: No such file or directory\n") % imagefile)
            else:
                if g_active_editor.filename:
                    root_path = os.path.dirname(g_active_editor.filename)
                
                relative_path = utils_path_to_relative(root_path, imagefile)
                text = importer_generate_image(relative_path, caption, label, scale)
                
                # Add required package
                editor_insert_package(g_active_editor, "graphicx", None)
                
                # Insert at current position
                current = Gtk.TextIter()
                editor_get_current_iter(g_active_editor, current)
                
                # Begin/end user action for undo
                g_e_buffer.begin_user_action()
                g_e_buffer.insert(current, text, len(text))
                g_e_buffer.end_user_action()
                g_e_buffer.set_modified(True)
                
                self.imagegui_set_sensitive("", False)
        
        # Close the panel
        self.remove_all_panels()
    
    def on_import_matrix_apply_clicked(self, widget, user_data=None):
        """Handle click on the apply button for matrix import.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        # Get current settings
        bracket = self.matrix_combobracket.get_active()
        rows = int(self.matrix_rows.get_value())
        cols = int(self.matrix_cols.get_value())
        
        # Generate matrix LaTeX code
        text = importer_generate_matrix(bracket, rows, cols)
        
        # Add required package
        editor_insert_package(g_active_editor, "amsmath", None)
        
        # Insert at current position
        current = Gtk.TextIter()
        editor_get_current_iter(g_active_editor, current)
        
        # Begin/end user action for undo
        g_e_buffer.begin_user_action()
        g_e_buffer.insert(current, text, len(text))
        g_e_buffer.end_user_action()
        g_e_buffer.set_modified(True)
        
        # Close the panel
        self.remove_all_panels()
    
    def on_import_biblio_apply_clicked(self, widget, user_data=None):
        """Handle click on the apply button for bibliography import.
        
        Args:
            widget: The button widget
            user_data: Additional data (unused)
        """
        filename = self.biblio_file.get_text()
        
        if filename and len(filename) > 0:
            root_path = None
            if g_active_editor.filename:
                root_path = os.path.dirname(g_active_editor.filename)
            
            relative_path = utils_path_to_relative(root_path, filename)
            
            # Insert bibliography reference
            from editor import editor_insert_bib
            editor_insert_bib(g_active_editor, relative_path)
            
            # Update the bibliography label
            basename = os.path.basename(filename)
            gummi.biblio.filenm_label.set_text(basename)
            
            # Clear the entry
            self.biblio_file.set_text("")
        
        # Close the panel
        self.remove_all_panels()
    
    def on_image_file_activate(self):
        """Handle activation of the image file entry."""
        from gui.gui_main import get_open_filename
        
        filename = get_open_filename(TYPE_IMAGE)
        if filename:
            self.imagegui_set_sensitive(filename, True)
    
    def on_biblio_file_activate(self, widget=None, user_data=None):
        """Handle activation of the bibliography file entry.
        
        Args:
            widget: The entry widget (unused)
            user_data: Additional data (unused)
        """
        from gui.gui_main import get_open_filename
        
        filename = get_open_filename(TYPE_BIBLIO)
        if filename:
            self.biblio_file.set_text(filename)
    
    def imagegui_set_sensitive(self, name, mode):
        """Set sensitivity of image import fields.
        
        Args:
            name: Name of the image file
            mode: True to enable fields, False to disable
        """
        self.image_label.set_sensitive(mode)
        self.image_caption.set_sensitive(mode)
        self.image_scale.set_sensitive(mode)
        
        self.image_file.set_text(name)
        self.image_label.set_text("")
        self.image_caption.set_text("")
        self.scaler.set_value(1.00)


def importgui_init(builder):
    """Initialize and return a new ImportGui instance.
    
    Args:
        builder: Gtk.Builder instance with the UI definitions
        
    Returns:
        A new ImportGui instance
    """
    if not isinstance(builder, Gtk.Builder):
        return None
    
    return ImportGui(builder)


def importgui_remove_all_panels():
    """Remove all panels from the import panel (wrapper function)."""
    if g_importgui:
        g_importgui.remove_all_panels()


def importer_imagegui_set_sensitive(name, mode):
    """Set sensitivity of image import fields (wrapper function).
    
    Args:
        name: Name of the image file
        mode: True to enable fields, False to disable
    """
    if g_importgui:
        g_importgui.imagegui_set_sensitive(name, mode)