#!/usr/bin/env python3
# silktex/application.py - Main application module

import os
import sys
import gi

# Set up required GObject Introspection libraries
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
try:
    gi.require_version('GtkSource', '5')
except ValueError:
    print("Warning: GtkSource 5 not available")

try:
    gi.require_version('WebKit', '6.0')
except ValueError:
    print("Warning: WebKit 6 not available")

from gi.repository import Gtk, Adw, Gio, GLib

# Initialize Adwaita
Adw.init()

class SilkTexApplication(Adw.Application):
    """Main application class for SilkTex"""
    
    def __init__(self, version="0.1.0"):
        super().__init__(application_id='org.example.silktex',
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        
        self.version = version
        
        # Set up actions
        self.create_action('quit', self.on_quit_action, ['<primary>q'])
        self.create_action('about', self.on_about_action)
        self.create_action('preferences', self.on_preferences_action, ['<primary>comma'])
        self.create_action('new', self.on_new_action, ['<primary>n'])
        self.create_action('open', self.on_open_action, ['<primary>o'])
        self.create_action('save', self.on_save_action, ['<primary>s'])
        self.create_action('save-as', self.on_save_as_action, ['<primary><shift>s'])
        self.create_action('compile', self.on_compile_action, ['F5'])
        
    def do_activate(self):
        """Handle application activation"""
        # Import window here to avoid circular imports
        from silktex.window import SilkTexWindow
        
        # Get the active window or create a new one
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self)
        
        # Present the window
        win.present()
    
    def on_quit_action(self, widget, _):
        """Handle quit action"""
        self.quit()
    
    def on_about_action(self, widget, _):
        """Show about dialog"""
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                           application_name='SilkTex',
                           application_icon='org.example.silktex',
                           developer_name='SilkTex Team',
                           version=self.version,
                           developers=['SilkTex Team'],
                           copyright='© 2023 SilkTex Team',
                           license_type=Gtk.License.GPL_3_0)
        about.present()
    
    def on_preferences_action(self, widget, _):
        """Show preferences dialog"""
        window = self.props.active_window
        if window:
            window.show_preferences()
    
    def on_new_action(self, widget, _):
        """Create a new document"""
        window = self.props.active_window
        if window:
            window.new_document()
    
    def on_open_action(self, widget, _):
        """Open a document"""
        window = self.props.active_window
        if window:
            window.open_document()
    
    def on_save_action(self, widget, _):
        """Save the current document"""
        window = self.props.active_window
        if window:
            window.save_document()
    
    def on_save_as_action(self, widget, _):
        """Save the current document as a new file"""
        window = self.props.active_window
        if window:
            window.save_document_as()
    
    def on_compile_action(self, widget, _):
        """Compile the current document"""
        window = self.props.active_window
        if window:
            window.compile_document()
    
    def create_action(self, name, callback, shortcuts=None):
        """Helper to create an action"""
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)
