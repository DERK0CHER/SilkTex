    def open_document(self):
        """Open a document"""
        # Check for unsaved changes first
        if self.editor_view.is_modified():
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Save Changes?",
                body="The current document has unsaved changes. Would you like to save them?",
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard")
            dialog.add_response("save", "Save")
            dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
            dialog.set_default_response("save")
            
            dialog.connect("response", self.on_open_document_save_response)
            dialog.present()
            return
        
        # Show file chooser dialog
        self.show_file_chooser_dialog(Gtk.FileChooserAction.OPEN, self.on_file_open_response)
    
    def on_open_document_save_response(self, dialog, response):
        """Handle save changes dialog response when opening a document"""
        if response == "save":
            # Save the document and then open
            saved = self.save_document()
            if saved:
                self.open_document()
        elif response == "discard":
            # Open without saving
            self.open_document()
        # else: Cancel - do nothing
    
    def show_file_chooser_dialog(self, action, callback):
        """Show a file chooser dialog"""
        # GTK4 file chooser
        dialog = Gtk.FileDialog()
        
        # Set dialog properties based on action
        if action == Gtk.FileChooserAction.OPEN:
            dialog.set_title("Open LaTeX Document")
            dialog.open(self, None, callback)
        elif action == Gtk.FileChooserAction.SAVE:
            dialog.set_title("Save LaTeX Document")
            if self.current_document and self.current_document.get_filename():
                dialog.set_initial_name(os.path.basename(self.current_document.get_filename()))
            else:
                dialog.set_initial_name("document.tex")
            dialog.save(self, None, callback)
    
    def on_file_open_response(self, dialog, result):
        """Handle file open dialog response"""
        try:
            file = dialog.open_finish(result)
            if file:
                # Get the path from the file
                file_path = file.get_path()
                self.load_document(file_path)
        except GLib.Error as error:
            toast = Adw.Toast(title=f"Error opening file: {error.message}")
            self.toast_overlay.add_toast(toast)
    
    def load_document(self, file_path):
        """Load a document from a file"""
        try:
            # Create new document from file
            self.current_document = Document(file_path)
            
            # Load content into editor
            content = self.current_document.get_content()
            self.editor_view.set_text(content)
            self.editor_view.get_buffer().set_modified(False)
            
            # Update UI
            self.update_title()
            
            # Update project sidebar
            project_dir = os.path.dirname(file_path)
            self.project_sidebar.set_project_directory(project_dir)
            
            # Trigger preview update
            self.update_preview(content)
            
            # Show success message
            filename = os.path.basename(file_path)
            toast = Adw.Toast(title=f"Opened {filename}")
            self.toast_overlay.add_toast(toast)
            
            # Update recent files
            self.add_to_recent_files(file_path)
            
            return True
        except Exception as e:
            # Show error message
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Error Opening File",
                body=f"Could not open file: {str(e)}",
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return False
    
    def save_document(self):
        """Save the document"""
        # If no filename set, save as
        if not self.current_document or not self.current_document.get_filename():
            return self.save_document_as()
        
        # Save content to file
        content = self.editor_view.get_text()
        try:
            self.current_document.set_content(content)
            self.current_document.save()
            self.editor_view.get_buffer().set_modified(False)
            
            # Update UI
            self.update_title()
            
            # Show success message
            filename = os.path.basename(self.current_document.get_filename())
            toast = Adw.Toast(title=f"Saved {filename}")
            self.toast_overlay.add_toast(toast)
            
            return True
        except Exception as e:
            # Show error message
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Error Saving File",
                body=f"Could not save file: {str(e)}",
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return False
    
    def save_document_as(self):
        """Save the document with a new name"""
        self.show_file_chooser_dialog(Gtk.FileChooserAction.SAVE, self.on_file_save_response)
        return False  # Will be saved asynchronously
    
    def on_file_save_response(self, dialog, result):
        """Handle file save dialog response"""
        try:
            file = dialog.save_finish(result)
            if file:
                # Get the path from the file
                file_path = file.get_path()
                
                # Create new document if necessary
                if not self.current_document:
                    self.current_document = Document()
                
                # Set the new filename
                self.current_document.set_filename(file_path)
                
                # Save content
                content = self.editor_view.get_text()
                self.current_document.set_content(content)
                self.current_document.save()
                self.editor_view.get_buffer().set_modified(False)
                
                # Update UI
                self.update_title()
                
                # Update project sidebar
                project_dir = os.path.dirname(file_path)
                self.project_sidebar.set_project_directory(project_dir)
                
                # Show success message
                filename = os.path.basename(file_path)
                toast = Adw.Toast(title=f"Saved as {filename}")
                self.toast_overlay.add_toast(toast)
                
                # Update recent files
                self.add_to_recent_files(file_path)
        except GLib.Error as error:
            toast = Adw.Toast(title=f"Error saving file: {error.message}")
            self.toast_overlay.add_toast(toast)
    
    def update_title(self):
        """Update window title based on current document"""
        if self.current_document:
            # Get the basename of the file
            if self.current_document.get_filename():
                basename = os.path.basename(self.current_document.get_filename())
                self.title_widget.set_title(basename)
                self.title_widget.set_subtitle(self.current_document.get_filename())
            else:
                self.title_widget.set_title("Untitled")
                self.title_widget.set_subtitle("Not saved")
            
            # Add asterisk if modified
            if self.editor_view.is_modified():
                current_title = self.title_widget.get_title()
                if not current_title.endswith("*"):
                    self.title_widget.set_title(current_title + "*")
        else:
            self.title_widget.set_title("SilkTex")
            self.title_widget.set_subtitle("")
    
    def compile_document(self):
        """Compile the current LaTeX document"""
        # Save document if needed
        if self.editor_view.is_modified():
            self.save_document()
        
        # Check if document is saved
        if not self.current_document or not self.current_document.get_filename():
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Cannot Compile",
                body="Please save the document before compiling.",
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return
        
        # Get the compilation engine
        selected = self.engine_combo.get_selected()
        engine = self.engine_model.get_string(selected)
        
        # Update status
        self.status_label.set_text("Compiling...")
        
        # Ensure we use standard icon names
        self.refresh_button = self.builder.get_object('refresh_button')
        if self.refresh_button:
            self.refresh_button.set_icon_name('view-refresh-symbolic')
            
        # Compile the document
        success, log = self.current_document.compile(engine)
        
        # Update preview
        self.preview_view.refresh()
        
        # Update status
        if success:
            self.status_label.set_text("Compilation successful")
            toast = Adw.Toast(title="Document compiled successfully")
        else:
            self.status_label.set_text("Compilation failed")
            toast = Adw.Toast(title="Compilation failed. See error log for details.")
            toast.set_button_label("View Log")
            toast.connect("button-clicked", lambda t: self.preview_view.show_error_log())
        
        self.toast_overlay.add_toast(toast)
    
    def update_preview(self, content=None):
        """Update the preview with current content"""
        if not content:
            content = self.editor_view.get_text()
        
        # Try to update the preview
        if self.preview_view:
            if self.current_document and self.current_document.get_filename():
                # For saved documents, we can show the compiled PDF
                self.preview_view.set_document(self.current_document)
            else:
                # For unsaved documents, we need to show a rendered preview
                self.preview_view.set_content(content)
        
        # Always return False so that GLib timeout doesn't repeat
        return False
    
    def add_to_recent_files(self, file_path):
        """Add a file to the recent files list"""
        recent_manager = Gtk.RecentManager.get_default()
        uri = GLib.filename_to_uri(file_path, None)
        recent_data = Gtk.RecentData()
        recent_data.display_name = os.path.basename(file_path)
        recent_data.description = "LaTeX Document"
        recent_data.mime_type = "text/x-tex"
        recent_data.app_name = self.get_application().get_application_id()
        recent_data.app_exec = f"{sys.argv[0]} %u"
        recent_manager.add_full(uri, recent_data)
    
    def show_preferences(self):
        """Show preferences dialog"""
        dialog = PreferencesDialog(self)
        dialog.present()
#!/usr/bin/env python3
# window.py - Window implementation for SilkTex

import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gio, GLib
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Main window module

This module defines the main application window and UI setup.
"""

import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Adw, Gio, GLib, GtkSource

from editor import SilkTexEditor
from preview import PDFPreview
from latex import LaTeXCompiler
from template import TemplateManager
from snippets import SnippetManager
from project import ProjectManager
from spell_checker import SpellChecker
from document_structure import DocumentStatistics

class SilkTexWindow(Adw.ApplicationWindow):
    """
    Main window for the SilkTex application.
    
    This class implements the main window which contains the editor,
    preview, and all UI elements.
    """
    
    def __init__(self, **kwargs):
        """Initialize the window and set up UI components."""
        super().__init__(**kwargs)
        
        # Window setup
        self.application = kwargs.get('application')
        self.set_default_size(1200, 800)
        self.set_title("SilkTex")
        
        # Initialize components
        self.setup_ui()
        self.setup_actions()
        
        # Create main components
        self.editor = SilkTexEditor(self)
        self.preview = PDFPreview(self)
        self.latex_compiler = LaTeXCompiler(self)
        self.template_manager = TemplateManager(self)
        self.snippet_manager = SnippetManager(self)
        self.project_manager = ProjectManager(self)
        self.spell_checker = SpellChecker(self)
        self.document_statistics = DocumentStatistics(self)
        
        # Set up paned view for editor and preview
        self.editor_preview_paned.set_start_child(self.editor)
        self.editor_preview_paned.set_end_child(self.preview)
        self.editor_preview_paned.set_position(600)
        
        # Connect signals
        self.editor.connect("buffer-changed", self.on_buffer_changed)
    
    def setup_ui(self):
        """Set up the user interface components."""
        # Create main container
        self.main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        
        # Header bar
        self.header = Adw.HeaderBar()
        self.main_box.append(self.header)
        
        # Menu button
        self.menu_button = Gtk.MenuButton()
        self.menu_button.set_icon_name("open-menu-symbolic")
        self.header.pack_end(self.menu_button)
        
        # Build main menu
        builder = Gtk.Builder.new_from_resource("/org/example/silktex/menu.ui")
        menu = builder.get_object("app-menu")
        self.menu_button.set_menu_model(menu)
        
        # Add document statistics button
        self.stats_button = Gtk.Button()
        self.stats_button.set_icon_name("accessories-calculator-symbolic")
        self.stats_button.set_tooltip_text("Document Statistics")
        self.stats_button.connect("clicked", self.on_stats_button_clicked)
        self.header.pack_end(self.stats_button)
        
        # Add template button
        self.template_button = Gtk.Button()
        self.template_button.set_icon_name("document-new-symbolic")
        self.template_button.set_tooltip_text("New from Template")
        self.template_button.connect("clicked", self.on_template_button_clicked)
        self.header.pack_start(self.template_button)
        
        # Main view (paned view for editor and preview)
        self.editor_preview_paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        self.main_box.append(self.editor_preview_paned)
        
        # Status bar
        self.status_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        self.status_bar.add_css_class("statusbar")
        self.status_label = Gtk.Label(label="Ready")
        self.status_bar.append(self.status_label)
        self.main_box.append(self.status_bar)
        
        # Set content
        self.set_content(self.main_box)
    
    def setup_actions(self):
        """Set up application actions."""
        # File actions
        action_group = Gio.SimpleActionGroup()
        
        # New file
        new_action = Gio.SimpleAction.new("new", None)
        new_action.connect("activate", self.on_new)
        action_group.add_action(new_action)
        
        # Open file
        open_action = Gio.SimpleAction.new("open", None)
        open_action.connect("activate", self.on_open)
        action_group.add_action(open_action)
        
        # Save file
        save_action = Gio.SimpleAction.new("save", None)
        save_action.connect("activate", self.on_save)
        action_group.add_action(save_action)
        
        # Save As
        save_as_action = Gio.SimpleAction.new("save-as", None)
        save_as_action.connect("activate", self.on_save_as)
        action_group.add_action(save_as_action)
        
        # Export PDF
        export_pdf_action = Gio.SimpleAction.new("export-pdf", None)
        export_pdf_action.connect("activate", self.on_export_pdf)
        action_group.add_action(export_pdf_action)
        
        # Quit
        quit_action = Gio.SimpleAction.new("quit", None)
        quit_action.connect("activate", self.on_quit)
        action_group.add_action(quit_action)
        
        # Insert snippet
        insert_snippet_action = Gio.SimpleAction.new("insert-snippet", None)
        insert_snippet_action.connect("activate", self.on_insert_snippet)
        action_group.add_action(insert_snippet_action)
        
        # Add action group to the window
        self.insert_action_group("win", action_group)
        
        # Add keyboard shortcuts
        self.application.set_accels_for_action("win.new", ["<Control>n"])
        self.application.set_accels_for_action("win.open", ["<Control>o"])
        self.application.set_accels_for_action("win.save", ["<Control>s"])
        self.application.set_accels_for_action("win.save-as", ["<Control><Shift>s"])
        self.application.set_accels_for_action("win.quit", ["<Control>q"])
    
    def on_new(self, action, param):
        """Create a new document."""
        # Check if we need to save current document
        if self.editor.is_modified():
            dialog = Adw.MessageDialog.new(
                self,
                "Save changes?",
                "The current document has unsaved changes. Save before creating a new document?"
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard")
            dialog.add_response("save", "Save")
            dialog.set_default_response("save")
            dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
            dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
            
            dialog.connect("response", self.on_new_dialog_response)
            dialog.present()
        else:
            self.editor.new_document()
    
    def on_new_dialog_response(self, dialog, response):
        """Handle response from the save dialog when creating a new document."""
        if response == "save":
            self.on_save(None, None)
            self.editor.new_document()
        elif response == "discard":
            self.editor.new_document()
    
    def on_open(self, action, param):
        """Open a document."""
        dialog = Gtk.FileChooserDialog(
            title="Open LaTeX Document",
            parent=self,
            action=Gtk.FileChooserAction.OPEN
        )
        dialog.add_button("Cancel", Gtk.ResponseType.CANCEL)
        dialog.add_button("Open", Gtk.ResponseType.ACCEPT)
        
        # Add file filters
        latex_filter = Gtk.FileFilter()
        latex_filter.set_name("LaTeX files")
        latex_filter.add_pattern("*.tex")
        dialog.add_filter(latex_filter)
        
        all_filter = Gtk.FileFilter()
        all_filter.set_name("All files")
        all_filter.add_pattern("*")
        dialog.add_filter(all_filter)
        
        dialog.connect("response", self.on_open_dialog_response)
        dialog.present()
    
    def on_open_dialog_response(self, dialog, response):
        """Handle response from the open dialog."""
        if response == Gtk.ResponseType.ACCEPT:
            file_path = dialog.get_file().get_path()
            self.open_file(file_path)
        dialog.destroy()
    
    def open_file(self, file_path):
        """Open the specified file."""
        self.editor.load_file(file_path)
        self.update_title()
    
    def on_save(self, action, param):
        """Save the current document."""
        if self.editor.get_file_path() is None:
            self.on_save_as(action, param)
        else:
            self.editor.save_file()
            self.update_title()
    
    def on_save_as(self, action, param):
        """Save the document with a new name."""
        dialog = Gtk.FileChooserDialog(
            title="Save LaTeX Document",
            parent=self,
            action=Gtk.FileChooserAction.SAVE
        )
        dialog.add_button("Cancel", Gtk.ResponseType.CANCEL)
        dialog.add_button("Save", Gtk.ResponseType.ACCEPT)
        dialog.set_do_overwrite_confirmation(True)
        
        # Add file filters
        latex_filter = Gtk.FileFilter()
        latex_filter.set_name("LaTeX files")
        latex_filter.add_pattern("*.tex")
        dialog.add_filter(latex_filter)
        
        all_filter = Gtk.FileFilter()
        all_filter.set_name("All files")
        all_filter.add_pattern("*")
        dialog.add_filter(all_filter)
        
        # Set current file name if available
        current_path = self.editor.get_file_path()
        if current_path:
            dialog.set_file(Gio.File.new_for_path(current_path))
        
        dialog.connect("response", self.on_save_dialog_response)
        dialog.present()
    
    def on_save_dialog_response(self, dialog, response):
        """Handle response from the save dialog."""
        if response == Gtk.ResponseType.ACCEPT:
            file_path = dialog.get_file().get_path()
            # Add .tex extension if none provided
            if not file_path.endswith(".tex") and dialog.get_filter() == dialog.get_filters()[0]:
                file_path += ".tex"
            self.editor.save_file_as(file_path)
            self.update_title()
        dialog.destroy()
    
    def on_export_pdf(self, action, param):
        """Export the document as PDF."""
        self.latex_compiler.compile_and_save_pdf()
    
    def on_quit(self, action, param):
        """Quit the application."""
        self.application.quit()
    
    def on_insert_snippet(self, action, param):
        """Insert a snippet at the current cursor position."""
        self.snippet_manager.show_snippet_selector()
    
    def on_stats_button_clicked(self, button):
        """Show document statistics dialog."""
        self.document_statistics.show_statistics()
    
    def on_template_button_clicked(self, button):
        """Show template selection dialog."""
        self.template_manager.show_template_selector()
    
    def on_buffer_changed(self, editor):
        """Handle changes to the editor buffer."""
        self.update_title()
        # Trigger LaTeX compilation for preview
        self.latex_compiler.compile_for_preview()
    
    def update_title(self):
        """Update the window title based on the current document."""
        file_path = self.editor.get_file_path()
        modified = self.editor.is_modified()
        
        if file_path:
            file_name = os.path.basename(file_path)
            self.set_title(f"{file_name}{' *' if modified else ''} - SilkTex")
        else:
            self.set_title(f"Untitled{' *' if modified else ''} - SilkTex")
    
    def update_status(self, message):
        """Update the status bar message."""
        self.status_label.set_text(message)
# Import the window class from silktex module
from silktex import SilkTexWindow

# This file exists to provide a bridge between the application
# and the actual window implementation in silktex.py