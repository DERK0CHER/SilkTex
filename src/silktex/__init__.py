#!/usr/bin/env python3
# silktex/__init__.py - Package initialization

"""
SilkTex - LaTeX editor with live preview
"""

import sys
import os

# Version information
__version__ = "0.1.0"

# Create a main function that we can call from setup.py entry_point
def main():
    """Main entry point for the application"""
    try:
        from silktex.application import SilkTexApplication
        app = SilkTexApplication(version=__version__)
        return app.run(sys.argv)
    except ImportError as e:
        print(f"Error starting SilkTex: {e}")
        print("Make sure all dependencies are installed.")
        return 1

# Export all relevant classes
try:
    # Import necessary modules
    from silktex.application import SilkTexApplication
    # For backward compatibility
    SilkTexApp = SilkTexApplication
    
    # Export classes
    __all__ = [
        'SilkTexApplication',
        'SilkTexApp',
        'main',
        '__version__'
    ]
    
except ImportError as e:
    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Adw', '1')
    from gi.repository import Gtk, Adw, Gio
    
    print(f"Warning: Failed to import SilkTex modules: {e}")
    
    # Create placeholder classes for better error handling
    class SilkTexApplication(Adw.Application):
        """Placeholder SilkTex application class"""
        def __init__(self, version="0.1.0"):
            super().__init__(application_id='org.example.silktex',
                             flags=Gio.ApplicationFlags.FLAGS_NONE)
            
            # Show error on startup
            self.connect('activate', self._show_error)
        
        def _show_error(self, app):
            """Show error dialog on startup"""
            window = Adw.ApplicationWindow(application=self)
            window.set_default_size(400, 200)
            window.set_title("SilkTex Error")
            
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            box.set_margin_top(20)
            box.set_margin_bottom(20)
            box.set_margin_start(20)
            box.set_margin_end(20)
            
            label = Gtk.Label()
            label.set_markup("<b>Error loading SilkTex</b>\n\nOne or more required modules could not be imported.")
            box.append(label)
            
            window.set_content(box)
            window.present()
    
    # For backward compatibility
    SilkTexApp = SilkTexApplication
    
    # Export classes
    __all__ = [
        'SilkTexApplication',
        'SilkTexApp',
        'main',
        '__version__'
    ]