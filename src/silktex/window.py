#!/usr/bin/env python3
# silktex/window.py - Main window module

import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gio, GLib

class SilkTexWindow(Adw.ApplicationWindow):
    """Main application window for SilkTex"""
    
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        
        # Basic window setup
        self.set_default_size(1000, 700)
        self.set_title("SilkTex")
        
        # Create a toast overlay and a box for content
        self.toast_overlay = Adw.ToastOverlay()
        self.set_content(self.toast_overlay)
        
        self.main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.toast_overlay.set_child(self.main_box)
        
        # Create a header bar
        self.header = Adw.HeaderBar()
        self.main_box.append(self.header)
        
        # Add a menu button
        menu = Gio.Menu.new()
        menu.append("New", "app.new")
        menu.append("Open", "app.open")
        menu.append("Save", "app.save")
        menu.append("Save As", "app.save-as")
        menu.append("Preferences", "app.preferences")
        menu.append("About", "app.about")
        menu.append("Quit", "app.quit")
        
        menu_button = Gtk.MenuButton()
        menu_button.set_icon_name("open-menu-symbolic")
        menu_button.set_menu_model(menu)
        self.header.pack_end(menu_button)
        
        # Create toolbar buttons
        new_button = Gtk.Button()
        new_button.set_icon_name("document-new-symbolic")
        new_button.set_tooltip_text("New Document")
        new_button.set_action_name("app.new")
        self.header.pack_start(new_button)
        
        open_button = Gtk.Button()
        open_button.set_icon_name("document-open-symbolic")
        open_button.set_tooltip_text("Open Document")
        open_button.set_action_name("app.open")
        self.header.pack_start(open_button)
        
        save_button = Gtk.Button()
        save_button.set_icon_name("document-save-symbolic")
        save_button.set_tooltip_text("Save Document")
        save_button.set_action_name("app.save")
        self.header.pack_start(save_button)
        
        # Main content area - welcome screen
        self.setup_welcome_screen()
        
        # Track current file
        self.current_file = None
    
    def setup_welcome_screen(self):
        """Setup the welcome screen"""
        self.welcome_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.welcome_box.set_margin_top(50)
        self.welcome_box.set_margin_bottom(50)
        self.welcome_box.set_margin_start(50)
        self.welcome_box.set_margin_end(50)
        self.welcome_box.set_vexpand(True)
        self.welcome_box.set_hexpand(True)
        self.welcome_box.set_valign(Gtk.Align.CENTER)
        self.welcome_box.set_halign(Gtk.Align.CENTER)
        
        # Add a logo or title
        title = Gtk.Label()
        title.set_markup("<span size='xx-large' weight='bold'>SilkTex</span>")
        self.welcome_box.append(title)
        
        subtitle = Gtk.Label()
        subtitle.set_markup("<span size='large'>LaTeX Editor with Live Preview</span>")
        self.welcome_box.append(subtitle)
        
        # Add some space
        spacer = Gtk.Box()
        spacer.set_size_request(-1, 20)
        self.welcome_box.append(spacer)
        
        # Add buttons for common actions
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_halign(Gtk.Align.CENTER)
        
        new_btn = Gtk.Button(label="New Document")
        new_btn.set_action_name("app.new")
        button_box.append(new_btn)
        
        open_btn = Gtk.Button(label="Open Document")
        open_btn.set_action_name("app.open")
        button_box.append(open_btn)
        
        self.welcome_box.append(button_box)
        
        # Add the welcome box to the main box
        self.main_box.append(self.welcome_box)
    
    def show_preferences(self):
        """Show preferences dialog"""
        toast = Adw.Toast(title="Preferences not implemented yet")
        self.toast_overlay.add_toast(toast)
    
    def new_document(self):
        """Create a new document"""
        toast = Adw.Toast(title="New document feature not implemented yet")
        self.toast_overlay.add_toast(toast)
    
    def open_document(self):
        """Open a document"""
        dialog = Gtk.FileDialog()
        dialog.set_title("Open LaTeX Document")
        
        filters = Gio.ListStore.new(Gtk.FileFilter)
        
        filter_tex = Gtk.FileFilter()
        filter_tex.set_name("LaTeX Files")
        filter_tex.add_pattern("*.tex")
        filters.append(filter_tex)
        
        filter_all = Gtk.FileFilter()
        filter_all.set_name("All Files")
        filter_all.add_pattern("*")
        filters.append(filter_all)
        
        dialog.set_filters(filters)
        
        # Use a lambda to capture self
        dialog.open(self, None, lambda dialog, result: self.on_open_dialog_response(dialog, result))
    
    def on_open_dialog_response(self, dialog, result):
        """Handle file open dialog response"""
        try:
            file = dialog.open_finish(result)
            if file:
                path = file.get_path()
                toast = Adw.Toast(title=f"Opening: {path}")
                self.toast_overlay.add_toast(toast)
                self.current_file = path
        except GLib.Error as error:
            print(f"Error opening file: {error.message}")
    
    def save_document(self):
        """Save the current document"""
        if self.current_file:
            toast = Adw.Toast(title=f"Saving to: {self.current_file}")
            self.toast_overlay.add_toast(toast)
        else:
            self.save_document_as()
    
    def save_document_as(self):
        """Save the current document as a new file"""
        dialog = Gtk.FileDialog()
        dialog.set_title("Save LaTeX Document")
        
        filters = Gio.ListStore.new(Gtk.FileFilter)
        
        filter_tex = Gtk.FileFilter()
        filter_tex.set_name("LaTeX Files")
        filter_tex.add_pattern("*.tex")
        filters.append(filter_tex)
        
        dialog.set_filters(filters)
        
        # Use a lambda to capture self
        dialog.save(self, None, lambda dialog, result: self.on_save_dialog_response(dialog, result))
    
    def on_save_dialog_response(self, dialog, result):
        """Handle save dialog response"""
        try:
            file = dialog.save_finish(result)
            if file:
                path = file.get_path()
                # Ensure .tex extension
                if not path.endswith('.tex'):
                    path += '.tex'
                
                toast = Adw.Toast(title=f"Saving to: {path}")
                self.toast_overlay.add_toast(toast)
                self.current_file = path
        except GLib.Error as error:
            print(f"Error saving file: {error.message}")
    
    def compile_document(self):
        """Compile the current document"""
        if self.current_file:
            toast = Adw.Toast(title=f"Compiling: {self.current_file}")
            self.toast_overlay.add_toast(toast)
        else:
            toast = Adw.Toast(title="No document to compile")
            self.toast_overlay.add_toast(toast)
