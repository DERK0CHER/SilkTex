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
import importlib.util
from pathlib import Path
from shutil import which

# Set up logging first
logger = logging.getLogger('silktex')
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.INFO)

def check_dependencies():
    """Check for required dependencies and report any missing ones."""
    try:
        # Check for GTK4
        import gi
        gi.require_version('Gtk', '4.0')
        gi.require_version('Adw', '1')
        gi.require_version('GtkSource', '5')
        try:
            gi.require_version('WebKit', '6.0')
        except ValueError:
            # WebKit is recommended but technically optional
            logger.warning("WebKit 6.0 not found. Preview functionality may be limited.")
        
        from gi.repository import Gtk, Adw, GtkSource
        
        # Initialize Adw
        Adw.init()
        
        # Check for LaTeX/XeTeX
        latex_found = which('pdflatex') is not None or which('xelatex') is not None
        if not latex_found:
            logger.warning("LaTeX executable not found. PDF preview will not work.")
            
        return True
    except (ImportError, ValueError) as e:
        logger.error(f"Failed to initialize GTK dependencies: {e}")
        print(f"Error: Required dependencies not found: {e}")
        print("Missing required dependencies. Please ensure you have:")
        print("  - GTK 4.0+")
        print("  - Libadwaita 1.0+")
        print("  - GtkSourceView 5.0+")
        print("  - LaTeX (pdflatex or xelatex) for PDF generation")
        return False

def setup_application_path():
    """Set up the Python path to include the application modules."""
    # Get the current directory
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Try to add the src directory to the path if running from source
    src_dir = os.path.join(current_dir, 'src')
    if os.path.isdir(src_dir) and src_dir not in sys.path:
        sys.path.insert(0, src_dir)
        logger.debug(f"Added source directory to path: {src_dir}")
    
    # Also add the current directory if it's not already in the path
    if current_dir not in sys.path:
        sys.path.insert(0, current_dir)
    
    # Check for installed module directories
    install_dirs = [
        os.path.join(current_dir, 'usr', 'local', 'share', 'silktex'),
        os.path.join(os.path.dirname(current_dir), 'share', 'silktex'),
        os.path.join('/usr', 'local', 'share', 'silktex'),
        os.path.join('/usr', 'share', 'silktex')
    ]
    
    for install_dir in install_dirs:
        if os.path.isdir(install_dir) and install_dir not in sys.path:
            sys.path.insert(0, install_dir)
            logger.debug(f"Added installation directory to path: {install_dir}")
            break

def import_silktex():
    """Import the silktex module using various strategies."""
    # Try various strategies to import silktex
    try:
        # First try direct import (might be installed via pip)
        import silktex
        logger.info("Imported silktex as installed package")
        return silktex
    except ImportError:
        pass
    
    try:
        # Try silktex from the src directory
        from src import silktex
        logger.info("Imported silktex from src directory")
        return silktex
    except ImportError:
        pass
    
    # Try importing from a specific file path
    current_dir = os.path.dirname(os.path.abspath(__file__))
    src_dir = os.path.join(current_dir, 'src')
    silktex_path = os.path.join(src_dir, "silktex.py")
    
    if os.path.exists(silktex_path):
        try:
            spec = importlib.util.spec_from_file_location("silktex", silktex_path)
            silktex = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(silktex)
            logger.info("Imported silktex from specific file path")
            return silktex
        except (ImportError, AttributeError) as e:
            logger.warning(f"Failed to import silktex from file: {e}")
    
    # If we get here, we couldn't import silktex
    logger.error("Failed to import silktex module by any method")
    print("Error: Could not import silktex module")
    print("Try installing the package with 'pip install -e .'")
    sys.exit(1)

def load_resources():
    """Load application resources from various potential locations."""
    from gi.repository import Gio
    
    # Determine potential resource locations
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    resource_locations = [
        # System-wide installation
        os.path.join(sys.prefix, 'share', 'silktex', 'silktex.gresource'),
        
        # Local development setup
        os.path.join(current_dir, 'data', 'resources', 'silktex.gresource'),
        os.path.join(current_dir, 'silktex.gresource'),
        os.path.join(os.path.dirname(current_dir), 'data', 'resources', 'silktex.gresource'),
        
        # Fallback locations
        'silktex.gresource',
        os.path.join('data', 'resources', 'silktex.gresource')
    ]
    
    # Try to load resources from any of the locations
    resource_loaded = False
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

def main(version=None):
    """Main entry point for the application
    
    Args:
        version: The application version string
        
    Returns:
        The application exit code
    """
    # Check for required dependencies
    if not check_dependencies():
        return 1
    
    # Import required GI libraries
    from gi.repository import GLib, Gio
    
    # Set up application paths
    setup_application_path()
    
    # Import silktex module
    silktex = import_silktex()
    
    # Initialize logging
    logger.info("Starting SilkTex")
    
    # Allow Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    try:
        # Create and run the application
        app = silktex.SilkTexApplication(version=version)
        
        # Set program name
        GLib.set_application_name('SilkTex')
        
        # Load UI resources
        load_resources()
        
        # Run the application
        return app.run(sys.argv)
    
    except Exception as e:
        logger.error(f"Error during application startup: {e}", exc_info=True)
        print(f"Error during application startup: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
