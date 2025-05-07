#!/usr/bin/env python3
# silktex/main.py - Package main entry point

import os
import sys
import signal
import logging
import gi

# Set up logging
logger = logging.getLogger('silktex')
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.INFO)

# Forward to the main script in the parent directory
def main(version=None):
    """Wrapper for the actual main function"""
    logger.info("Starting SilkTex from package")
    
    # Enable Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    try:
        # First look for silktex.py in the parent directory
        parent_dir = os.path.dirname(os.path.dirname(__file__))
        silktex_path = os.path.join(parent_dir, 'silktex.py')
        
        if os.path.exists(silktex_path):
            logger.info(f"Found silktex.py at {silktex_path}")
            
            # Add parent directory to path
            if parent_dir not in sys.path:
                sys.path.insert(0, parent_dir)
            
            # Try to import directly
            try:
                import silktex
                logger.info("Successfully imported silktex module")
                
                # Create and run the application
                app = silktex.SilkTexApplication(version=version)
                return app.run(sys.argv)
            except ImportError as e:
                logger.warning(f"Could not import silktex module: {e}")
        
        # If that fails, try to load main.py
        main_path = os.path.join(parent_dir, 'main.py')
        
        if os.path.exists(main_path):
            logger.info(f"Found main.py at {main_path}")
            
            # Try to import as a module
            try:
                if parent_dir not in sys.path:
                    sys.path.insert(0, parent_dir)
                import main as main_module
                logger.info("Successfully imported main module")
                return main_module.main(version)
            except ImportError as e:
                logger.warning(f"Could not import main module: {e}")
                
                # Try direct execution as a fallback
                logger.info("Trying direct execution of main.py")
                with open(main_path) as f:
                    globals_dict = {'__file__': main_path}
                    exec(compile(f.read(), main_path, 'exec'), globals_dict)
                    if 'main' in globals_dict:
                        return globals_dict['main'](version)
        
        # If we get here, we couldn't find the main module
        logger.error("Could not find or execute main module")
        
        # As a last resort, fall back to our own minimal implementation
        logger.info("Creating minimal fallback application")
        
        # Set up required GObject Introspection libraries
        gi.require_version('Gtk', '4.0')
        gi.require_version('Adw', '1')
        from gi.repository import Gtk, Adw, Gio, GLib
        
        # Initialize Adw
        Adw.init()
        
        # Create a basic application
        app = Adw.Application(application_id='org.example.silktex',
                              flags=Gio.ApplicationFlags.FLAGS_NONE)
        
        # Define activate handler
        def on_activate(app):
            win = Gtk.ApplicationWindow(application=app, title="SilkTex - Emergency Mode")
            win.set_default_size(600, 400)
            
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            win.set_child(box)
            
            header = Adw.HeaderBar()
            box.append(header)
            
            content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
            content.set_margin_top(50)
            content.set_margin_bottom(50)
            content.set_margin_start(50)
            content.set_margin_end(50)
            content.set_vexpand(True)
            content.set_valign(Gtk.Align.CENTER)
            box.append(content)
            
            title = Gtk.Label()
            title.set_markup("<span size='xx-large' weight='bold'>SilkTex</span>")
            content.append(title)
            
            error_msg = Gtk.Label()
            error_msg.set_markup("<span foreground='red'>Emergency Mode</span>")
            content.append(error_msg)
            
            details = Gtk.Label(label="Could not load the main application module.\n"
                                "This is a minimal fallback application.")
            details.set_wrap(True)
            content.append(details)
            
            win.present()
        
        # Connect signal
        app.connect('activate', on_activate)
        
        # Run the application
        return app.run(sys.argv)
        
    except Exception as e:
        logger.error(f"Error in silktex.main: {e}", exc_info=True)
        print(f"Error in silktex.main: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
