    def on_about_action(self, widget, _):
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                                application_name='SilkTex',
                                application_icon='document-edit-symbolic',
                                developer_name='Developer',
                                version='0.1.0',
                                developers=['Your Name'],
                                copyright='© 2023 Your Name')
        about.present()
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Application module

This module defines the main SilkTexApplication class which handles the
application lifecycle, window creation, and command-line arguments.
"""

import os
import sys
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gio, GLib

from window import SilkTexWindow
from config import ConfigManager

class SilkTexApplication(Adw.Application):
    """
    Main application class for SilkTex.
    Handles application lifecycle and window management.
    """
    
    def __init__(self, version='0.1.0'):
        """Initialize the application with the correct application ID."""
        super().__init__(
            application_id='org.example.silktex',
            flags=Gio.ApplicationFlags.HANDLES_OPEN
        )
        
        self.version = version
        self.window = None
        self.config = ConfigManager()
        
        # Add command line options
        self.add_main_option(
            'version', ord('v'), 
            GLib.OptionFlags.NONE,
            GLib.OptionArg.NONE,
            'Print version information and exit',
            None
        )
        
        self.connect('open', self.on_open)
        self.connect('activate', self.on_activate)
        self.connect('handle-local-options', self.on_handle_local_options)
    
    def on_handle_local_options(self, application, options):
        """Handle command line options before activation."""
        if options.contains('version'):
            print(f"SilkTex version {self.version}")
            return 0
        
        return -1  # Continue normal application processing
    
    def on_open(self, application, files, n_files, hint):
        """
        Handle opening files from command line or file manager.
        
        Args:
            application: The application instance
            files: Array of Gio.File objects
            n_files: Number of files
            hint: String containing additional information
        """
        self.on_activate(application)
        
        # Open each file in a new tab
        for file in files:
            self.window.open_file(file.get_path())
    
    def on_activate(self, application):
        """
        Handle application activation, either creating a new window
        or focusing an existing one.
        
        Args:
            application: The application instance
        """
        # Setup resources and styling
        self.setup_resources()
        
        # Get the active window or create a new one
        if not self.window:
            self.window = SilkTexWindow(application=self)
            
        self.window.present()
    
    def setup_resources(self):
        """Set up application resources and CSS styling."""
        # Add CSS styling
        css_provider = Gtk.CssProvider()
        try:
            # Look in several places for the CSS file
            css_paths = [
                os.path.join(os.path.dirname(__file__), '..', 'data', 'style.css'),
                os.path.join(os.path.dirname(__file__), 'data', 'style.css'),
                '/usr/share/silktex/data/style.css',
                '/usr/local/share/silktex/data/style.css'
            ]
            
            for css_path in css_paths:
                if os.path.exists(css_path):
                    css_provider.load_from_path(css_path)
                    Gtk.StyleContext.add_provider_for_display(
                        Gdk.Display.get_default(),
                        css_provider,
                        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
                    )
                    break
        except Exception as e:
            print(f"Warning: Could not load CSS: {e}")