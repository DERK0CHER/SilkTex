#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Main application entry point

This module serves as the main entry point for the SilkTex application.
It handles initialization, dependency checking, and launching the
application interface.
"""

import os
import sys
import signal
import logging
import argparse
import importlib.util
from pathlib import Path

# Set up logging first
logger = logging.getLogger('silktex')
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.INFO)

# Check for required GObject dependencies
try:
    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Adw', '1')
    gi.require_version('GtkSource', '5')
    try:
        gi.require_version('WebKit', '6.0')
    except ValueError:
        # WebKit is recommended but technically optional
        logger.warning("WebKit 6.0 not found. Preview functionality may be limited.")
    
    from gi.repository import Gtk, Adw, Gio, GLib
    # Initialize Adw
    Adw.init()
except (ImportError, ValueError) as e:
    logger.error(f"Failed to initialize GTK dependencies: {e}")
    print(f"Error: Required dependencies not found: {e}")
    print("Please ensure you have GTK 4, libadwaita, and gtksourceview installed.")
    sys.exit(1)

def setup_application_path():
    """Set up Python path to find application modules"""
    # Add the current directory to the path if running from source
    current_dir = os.path.dirname(os.path.abspath(__file__))
    parent_dir = os.path.dirname(current_dir)
    
    # Add current directory to path if not already there
    if current_dir not in sys.path:
        sys.path.insert(0, current_dir)
    
    # If we have a silktex directory in current_dir, we're likely in the src/ folder
    # with the silktex package as a subdirectory
    if os.path.isdir(os.path.join(current_dir, 'silktex')):
        logger.info("Running from source directory structure")
        return True
    
    # We might also be in the project root, with src/ as a subdirectory
    if os.path.isdir(os.path.join(parent_dir, 'src')):
        src_dir = os.path.join(parent_dir, 'src')
        if src_dir not in sys.path:
            sys.path.insert(0, src_dir)
        logger.info(f"Added source directory to path: {src_dir}")
        return True
    
    return False

def import_silktex():
    """Import SilkTex modules using various strategies"""
    # Try multiple import strategies to find the silktex module
    
    # 1. First try direct import (if installed via pip)
    try:
        import silktex
        logger.info("Imported silktex module (installed package)")
        return silktex
    except ImportError:
        pass
    
    # 2. Try to import from current directory's silktex.py directly
    try:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        silktex_path = os.path.join(current_dir, "silktex.py")
        if os.path.exists(silktex_path):
            spec = importlib.util.spec_from_file_location("silktex", silktex_path)
            silktex = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(silktex)
            logger.info("Imported silktex from local silktex.py")
            return silktex
    except (ImportError, AttributeError) as e:
        logger.warning(f"Could not import from silktex.py: {e}")
    
    # 3. Try to import from silktex subdirectory in current directory
    try:
        import silktex
        logger.info("Imported silktex module from local package")
        return silktex
    except ImportError:
        pass
    
    # 4. As a last resort, create minimal module with required classes
    logger.error("Failed to import silktex module by any method")
    print("Error: Could not import silktex module")
    print("Please ensure SilkTex is properly installed or run from source directory")
    sys.exit(1)

def load_resources():
    """Locate and load application resources"""
    # Try to find resources in various locations
    resource_loaded = False
    
    # Determine potential resource locations
    current_dir = os.path.dirname(os.path.abspath(__file__))
    parent_dir = os.path.dirname(current_dir)
    
    resource_locations = [
        # System-wide installation
        os.path.join(sys.prefix, 'share', 'silktex', 'silktex.gresource'),
        
        # Local development setup
        os.path.join(current_dir, 'silktex.gresource'),
        os.path.join(current_dir, '..', 'data', 'resources', 'silktex.gresource'),
        os.path.join(parent_dir, 'data', 'resources', 'silktex.gresource'),
        
        # Fallback locations
        os.path.join(current_dir, 'data', 'resources', 'silktex.gresource'),
        'silktex.gresource'
    ]
    
    for path in resource_locations:
        if os.path.exists(path):
            try:
                logger.info(f"Loading resources from {path}")
                resource = Gio.Resource.load(path)
                Gio.resources_register(resource)
                resource_loaded = True
                break
            except Exception as e:
                logger.warning(f"Failed to load resource from {path}: {e}")
    
    if not resource_loaded:
        logger.warning("No resources loaded, UI may not display correctly")
    
    return resource_loaded

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description='SilkTex - LaTeX editor with live preview')
    parser.add_argument('--version', '-v', action='store_true', help='Show version information and exit')
    parser.add_argument('--debug', '-d', action='store_true', help='Enable debug logging')
    parser.add_argument('file', nargs='?', help='LaTeX file to open')
    
    return parser.parse_args()

def main():
    """Main entry point for the application"""
    # Parse command-line arguments
    args = parse_arguments()
    
    # Set debug logging if requested
    if args.debug:
        logger.setLevel(logging.DEBUG)
        logger.debug("Debug logging enabled")
    
    # Set up application path
    setup_application_path()
    
    # Import silktex module
    silktex = import_silktex()
    
    # Show version if requested
    if args.version:
        if hasattr(silktex, '__version__'):
            print(f"SilkTex version {silktex.__version__}")
        else:
            print("SilkTex development version")
        return 0
    
    # Log startup
    logger.info("Starting SilkTex")
    
    # Allow Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    try:
        # Create application instance
        app = silktex.SilkTexApplication(version="0.1.0")
        
        # Set application name
        GLib.set_application_name('SilkTex')
        
        # Load UI resources
        load_resources()
        
        # If a file was specified, prepare to open it
        if args.file:
            # Store the file to open after startup
            app.file_to_open = os.path.abspath(args.file)
        
        # Run the application
        return app.run(sys.argv)
        
    except Exception as e:
        logger.error(f"Error during application startup: {e}", exc_info=True)
        print(f"Error during application startup: {e}")
        return 1

if __name__ == '__main__':
    sys.exit(main())
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Main application entry point

This module serves as the main entry point for the SilkTex application.
It handles initialization, dependency checking, and launching the
application interface.
"""

import os
import sys
import signal
import logging
import argparse
import importlib.util
from pathlib import Path

# Set up logging first
logger = logging.getLogger('silktex')
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.INFO)

# Check for required GObject dependencies
try:
    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Adw', '1')
    gi.require_version('GtkSource', '5')
    try:
        gi.require_version('WebKit', '6.0')
    except ValueError:
        # WebKit is recommended but technically optional
        logger.warning("WebKit 6.0 not found. Preview functionality may be limited.")
    
    from gi.repository import Gtk, Adw, Gio, GLib
    # Initialize Adw
    Adw.init()
except (ImportError, ValueError) as e:
    logger.error(f"Failed to initialize GTK dependencies: {e}")
    print(f"Error: Required dependencies not found: {e}")
    print("Please ensure you have GTK 4, libadwaita, and gtksourceview installed.")
    sys.exit(1)

def setup_application_path():
    """Set up Python path to find application modules"""
    # Add the current directory to the path if running from source
    current_dir = os.path.dirname(os.path.abspath(__file__))
    parent_dir = os.path.dirname(current_dir)
    
    # Add current directory to path if not already there
    if current_dir not in sys.path:
        sys.path.insert(0, current_dir)
    
    # If we have a silktex directory in current_dir, we're likely in the src/ folder
    # with the silktex package as a subdirectory
    if os.path.isdir(os.path.join(current_dir, 'silktex')):
        logger.info("Running from source directory structure")
        return True
    
    # We might also be in the project root, with src/ as a subdirectory
    if os.path.isdir(os.path.join(parent_dir, 'src')):
        src_dir = os.path.join(parent_dir, 'src')
        if src_dir not in sys.path:
            sys.path.insert(0, src_dir)
        logger.info(f"Added source directory to path: {src_dir}")
        return True
    
    return False

def import_silktex():
    """Import SilkTex modules using various strategies"""
    # Try multiple import strategies to find the silktex module
    
    # 1. First try direct import (if installed via pip)
    try:
        import silktex
        logger.info("Imported silktex module (installed package)")
        return silktex
    except ImportError:
        pass
    
    # 2. Try to import from current directory's silktex.py directly
    try:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        silktex_path = os.path.join(current_dir, "silktex.py")
        if os.path.exists(silktex_path):
            spec = importlib.util.spec_from_file_location("silktex", silktex_path)
            silktex = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(silktex)
            logger.info("Imported silktex from local silktex.py")
            return silktex
    except (ImportError, AttributeError) as e:
        logger.warning(f"Could not import from silktex.py: {e}")
    
    # 3. Try to import from silktex subdirectory in current directory
    try:
        import silktex
        logger.info("Imported silktex module from local package")
        return silktex
    except ImportError:
        pass
    
    # 4. As a last resort, create minimal module with required classes
    logger.error("Failed to import silktex module by any method")
    print("Error: Could not import silktex module")
    print("Please ensure SilkTex is properly installed or run from source directory")
    sys.exit(1)

def load_resources():
    """Locate and load application resources"""
    # Try to find resources in various locations
    resource_loaded = False
    
    # Determine potential resource locations
    current_dir = os.path.dirname(os.path.abspath(__file__))
    parent_dir = os.path.dirname(current_dir)
    
    resource_locations = [
        # System-wide installation
        os.path.join(sys.prefix, 'share', 'silktex', 'silktex.gresource'),
        
        # Local development setup
        os.path.join(current_dir, 'silktex.gresource'),
        os.path.join(current_dir, '..', 'data', 'resources', 'silktex.gresource'),
        os.path.join(parent_dir, 'data', 'resources', 'silktex.gresource'),
        
        # Fallback locations
        os.path.join(current_dir, 'data', 'resources', 'silktex.gresource'),
        'silktex.gresource'
    ]
    
    for path in resource_locations:
        if os.path.exists(path):
            try:
                logger.info(f"Loading resources from {path}")
                resource = Gio.Resource.load(path)
                Gio.resources_register(resource)
                resource_loaded = True
                break
            except Exception as e:
                logger.warning(f"Failed to load resource from {path}: {e}")
    
    if not resource_loaded:
        logger.warning("No resources loaded, UI may not display correctly")
    
    return resource_loaded

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description='SilkTex - LaTeX editor with live preview')
    parser.add_argument('--version', '-v', action='store_true', help='Show version information and exit')
    parser.add_argument('--debug', '-d', action='store_true', help='Enable debug logging')
    parser.add_argument('file', nargs='?', help='LaTeX file to open')
    
    return parser.parse_args()

def main():
    """Main entry point for the application"""
    # Parse command-line arguments
    args = parse_arguments()
    
    # Set debug logging if requested
    if args.debug:
        logger.setLevel(logging.DEBUG)
        logger.debug("Debug logging enabled")
    
    # Set up application path
    setup_application_path()
    
    # Import silktex module
    silktex = import_silktex()
    
    # Show version if requested
    if args.version:
        if hasattr(silktex, '__version__'):
            print(f"SilkTex version {silktex.__version__}")
        else:
            print("SilkTex development version")
        return 0
    
    # Log startup
    logger.info("Starting SilkTex")
    
    # Allow Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    try:
        # Create application instance
        app = silktex.SilkTexApplication(version="0.1.0")
        
        # Set application name
        GLib.set_application_name('SilkTex')
        
        # Load UI resources
        load_resources()
        
        # If a file was specified, prepare to open it
        if args.file:
            # Store the file to open after startup
            app.file_to_open = os.path.abspath(args.file)
        
        # Run the application
        return app.run(sys.argv)
        
    except Exception as e:
        logger.error(f"Error during application startup: {e}", exc_info=True)
        print(f"Error during application startup: {e}")
        return 1

if __name__ == '__main__':
    sys.exit(main())