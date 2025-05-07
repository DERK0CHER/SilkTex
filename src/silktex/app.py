#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - A modern LaTeX editor with live preview
Application class definition
"""

import sys
import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')

from gi.repository import Gtk, Gio, GLib, Adw

from silktex.window import SilkTexWindow
from silktex.config import Config


class SilkTexApplication(Adw.Application):
    """SilkTex application class"""
    
    def __init__(self):
        """Initialize the application"""
        super().__init__(
            application_id="org.silktex.SilkTex",
            flags=Gio.ApplicationFlags.HANDLES_OPEN
        )
        
        # Application settings
        self.config = Config()
        
        # Command line options
        self.add_main_option(
            "version", ord("v"), 
            GLib.OptionFlags.NONE, GLib.OptionArg.NONE,
            "Show application version", None
        )
        
    def do_startup(self):
        """Application startup handler"""
        Adw.Application.do_startup(self)
        
        # Set application actions
        self.create_action("quit", self.on_quit, ["<primary>q"])
        self.create_action("about", self.on_about)
        self.create_action("preferences", self.on_preferences, ["<primary>comma"])
        self.create_action("new", self.on_new, ["<primary>n"])
        self.create_action("open", self.on_open, ["<primary>o"])
        
    def do_activate(self):
        """Application activation handler"""
        # Get active window or create one if none exists
        window = self.props.active_window
        if not window:
            window = SilkTexWindow(application=self)
        
        window.present()
    
    def do_open(self, files, n_files, hint):
        """Handle file opening"""
        # Get active window or create one if none exists
        window = self.props.active_window
        if not window:
            window = SilkTexWindow(application=self)
        
        for gfile in files:
            window.open_file(gfile)
        
        window.present()
    
    def do_handle_local_options(self, options):
        """Handle local command line options"""
        if options.contains("version"):
            print("SilkTex version 0.1.0")
            return 0
        
        return -1
    
    def create_action(self, name, callback, shortcuts=None):
        """Add an application action
        
        Args:
            name: Action name
            callback: Action callback
            shortcuts: Optional list of accelerator shortcuts
        """
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)
    
    def on_about(self, action, param):
        """Show about dialog"""
        about = Adw.AboutWindow(
            transient_for=self.props.active_window,
            application_name="SilkTex",
            application_icon="org.silktex.SilkTex",
            developer_name="SilkTex Team",
            version="0.1.0",
            developers=["SilkTex Team"],
            copyright="© 2023 SilkTex Team",
            license_type=Gtk.License.MIT_X11,
            website="https://github.com/silktex/silktex",
            issue_url="https://github.com/silktex/silktex/issues"
        )
        about.present()
    
    def on_preferences(self, action, param):
        """Show preferences dialog"""
        # TODO: Implement preferences dialog
        pass
    
    def on_quit(self, action, param):
        """Quit the application"""
        self.quit()
    
    def on_new(self, action, param):
        """Create a new document"""
        window = self.props.active_window
        if window:
            window.new_document()
    
    def on_open(self, action, param):
        """Open a document"""
        window = self.props.active_window
        if window:
            window.open_document()
