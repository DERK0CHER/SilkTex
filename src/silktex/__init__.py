# __init__.py - Package initialization for silktex module
"""SilkTex - A lightweight LaTeX editor"""
"""
SilkTex - LaTeX editor with live preview
"""

# Import main classes for easy access
from ..main import main

# Create the SilkTexApp class that's referenced in the main.py file
from gi.repository import Gtk, Adw, Gio, GLib

class SilkTexApp(Adw.Application):
    """Main application class for SilkTex"""
    
    def __init__(self, application_id):
        super().__init__(application_id=application_id,
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        self.window = None
        self.version = "0.1.0"  # Default version
        
    def do_activate(self):
        """Handle application activation"""
        try:
            # Try to import the window class
            from .window import SilkTexWindow
            self.window = SilkTexWindow(application=self)
            self.window.present()
        except ImportError:
            # If window class is not available, show a basic window
            self.window = Gtk.ApplicationWindow(application=self, title="SilkTex")
            self.window.set_default_size(800, 600)
            
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            box.set_margin_top(50)
            box.set_margin_bottom(50)
            box.set_margin_start(50)
            box.set_margin_end(50)
            
            label = Gtk.Label()
            label.set_markup("<span size='xx-large'>SilkTex</span>")
            box.append(label)
            
            info_label = Gtk.Label(label="LaTeX editor with live preview")
            box.append(info_label)
            
            self.window.set_child(box)
            self.window.present()
__version__ = "0.1.0"
