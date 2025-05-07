#!/usr/bin/env python3
# main.py - Basic launcher for SilkTex

import os
import sys
import signal
import gi

# Set up required GObject Introspection libraries
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
try:
    gi.require_version('GtkSource', '5')
except ValueError:
    print("Warning: GtkSource 5 not available")

from gi.repository import Gtk, Adw, Gio, GLib

# Initialize Adw
Adw.init()

# Simple window class that will always work
class SilkTexWindow(Adw.ApplicationWindow):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        
        # Basic window setup
        self.set_default_size(1000, 700)
        self.set_title("SilkTex")
        
        # Create a box for content
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.set_content(self.box)
        
        # Create a header bar
        self.header = Adw.HeaderBar()
        self.box.append(self.header)
        
        # Add a menu button
        menu = Gio.Menu.new()
        menu.append("About", "app.about")
        menu.append("Quit", "app.quit")
        
        menu_button = Gtk.MenuButton()
        menu_button.set_icon_name("open-menu-symbolic")
        menu_button.set_menu_model(menu)
        self.header.pack_end(menu_button)
        
        # Main content - a welcome message
        self.content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        self.content_box.set_halign(Gtk.Align.CENTER)
        self.content_box.set_valign(Gtk.Align.CENTER)
        self.content_box.set_margin_top(50)
        self.content_box.set_margin_bottom(50)
        self.content_box.set_margin_start(50)
        self.content_box.set_margin_end(50)
        self.box.append(self.content_box)
        
        label1 = Gtk.Label()
        label1.set_markup("<span size='xx-large' weight='bold'>SilkTex</span>")
        self.content_box.append(label1)
        
        label2 = Gtk.Label(label="LaTeX Editor with Live Preview")
        self.content_box.append(label2)
        
        # Add a message about paths
        label3 = Gtk.Label()
        label3.set_markup("<span size='small'>Running in compatibility mode</span>")
        self.content_box.append(label3)
        
        # Create some buttons for actions
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_halign(Gtk.Align.CENTER)
        button_box.set_margin_top(20)
        self.content_box.append(button_box)
        
        new_btn = Gtk.Button(label="New Document")
        new_btn.connect("clicked", self.on_new_clicked)
        button_box.append(new_btn)
        
        open_btn = Gtk.Button(label="Open Document")
        open_btn.connect("clicked", self.on_open_clicked)
        button_box.append(open_btn)
    
    def on_new_clicked(self, button):
        dialog = Adw.MessageDialog(
            transient_for=self,
            heading="New Document",
            body="This feature is not yet implemented in compatibility mode.",
            close_response="ok"
        )
        dialog.add_response("ok", "OK")
        dialog.present()
    
    def on_open_clicked(self, button):
        dialog = Adw.MessageDialog(
            transient_for=self,
            heading="Open Document",
            body="This feature is not yet implemented in compatibility mode.",
            close_response="ok"
        )
        dialog.add_response("ok", "OK")
        dialog.present()


class SilkTexApplication(Adw.Application):
    def __init__(self, version="0.1.0"):
        super().__init__(application_id='org.example.silktex',
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        self.version = version
        
        # Create actions
        self.create_action('quit', self.on_quit_action, ['<primary>q'])
        self.create_action('about', self.on_about_action)
    
    def do_activate(self):
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self)
        win.present()
    
    def on_quit_action(self, widget, _):
        self.quit()
    
    def on_about_action(self, widget, _):
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                               application_name='SilkTex',
                               application_icon='org.example.silktex',
                               developer_name='SilkTex Team',
                               version=self.version,
                               copyright='© 2023 SilkTex Team')
        about.present()
    
    def create_action(self, name, callback, shortcuts=None):
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)


def main(version=None):
    """Main entry point for the application"""
    # Enable Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    # Set program name
    GLib.set_application_name('SilkTex')
    
    # Create and run the application
    app = SilkTexApplication(version=version or "0.1.0")
    return app.run(sys.argv)


if __name__ == "__main__":
    sys.exit(main())
