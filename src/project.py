"""
project.py - Project management for Gummi

Copyright (C) 2009-2025 Gummi Developers
All Rights reserved.

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
"""

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource, GLib

import os
import logging

from config import Config
from utils import Utils

# TODO: Refactor to remove direct access to gui and gummi structure
# These would be imported from appropriate modules
# For now, let's assume they're available as global variables
# from gui.main import gui
# from gummi import gummi

class GuProject:
    """Project management class for Gummi"""
    
    def __init__(self):
        """Initialize a new project instance"""
        self.projfile = None
        self.rootfile = None
        self.nroffiles = 1
        
    def create_new(self, filename):
        """Create a new project file
        
        Args:
            filename: Path to the new project file
            
        Returns:
            bool: True if the project was created successfully
        """
        version = "0.6.0"
        csetter = Config.get_string("Compile", "typesetter")
        csteps = Config.get_string("Compile", "steps")
        
        # Get the active editor's filename
        rootfile = gui.active_editor.filename
        
        # Format the project file content
        content = f"version={version}\n" \
                 f"typesetter={csetter}\n" \
                 f"steps={csteps}\n" \
                 f"root={rootfile}\n"
        
        # Add .gummi extension if not present
        if not filename.endswith(".gummi"):
            filename = f"{filename}.gummi"
        
        gui.statusbar.set_message(f"Creating project file: {filename}")
        Utils.set_file_contents(filename, content)
        
        self.projfile = filename
        
        return True
    
    def open_existing(self, filename):
        """Open an existing project file
        
        Args:
            filename: Path to the project file
            
        Returns:
            bool: True if the project was opened successfully
        """
        try:
            with open(filename, 'r') as f:
                content = f.read()
        except Exception as e:
            logging.error(f"Error opening project file: {e}")
            return False
        
        if not self.check_file_integrity(content):
            return False
        
        if not self.load_files(filename, content):
            return False
        
        self.projfile = filename
        
        return True
    
    def close(self):
        """Close the current project
        
        Returns:
            bool: True if the project was closed successfully
        """
        tabs = gummi.get_all_tabs().copy()
        
        # Disable compile thread to prevent it from compiling nonexisting editor
        gummi.motion.stop_compile_thread()
        gummi.tabmanager.set_active_tab(-1)
        
        for tab in tabs:
            if tab.editor.projfile is not None:
                gummi.on_menu_close_activate(None, tab)
        
        # Resume compile by selecting an active tab
        if gummi.get_all_tabs():
            gummi.tabmanager.set_active_tab(0)
        
        gummi.motion.start_compile_thread()
        
        return True
    
    def check_file_integrity(self, content):
        """Check if the project file content is valid
        
        Args:
            content: Project file content
            
        Returns:
            bool: True if the content is valid
        """
        return len(content) > 0
    
    def add_document(self, project, fname):
        """Add a document to the project
        
        Args:
            project: Path to the project file
            fname: Path to the document to add
            
        Returns:
            bool: True if the document was added successfully
        """
        try:
            with open(project, 'r') as f:
                oldcontent = f.read()
        except Exception as e:
            logging.error(f"Error reading project file: {e}")
            return False
        
        # Don't add files that are already in the project
        if Utils.subinstr(fname, oldcontent, True):
            return False
        
        newcontent = oldcontent + f"\nfile={fname}"
        
        if os.path.exists(project):
            Utils.set_file_contents(project, newcontent)
            return True
        
        return False
    
    def remove_document(self, project, fname):
        """Remove a document from the project
        
        Args:
            project: Path to the project file
            fname: Path to the document to remove
            
        Returns:
            bool: True if the document was removed successfully
        """
        try:
            with open(project, 'r') as f:
                oldcontent = f.read()
        except Exception as e:
            logging.error(f"Error reading project file: {e}")
            return False
        
        delimiter = f"file={fname}"
        split_content = oldcontent.split(delimiter, 1)
        
        if len(split_content) < 2:
            return False
            
        newcontent = split_content[0] + split_content[1]
        
        if os.path.exists(project):
            Utils.set_file_contents(project, newcontent)
            return True
        
        return False
    
    def list_files(self, content):
        """List all files in the project
        
        Args:
            content: Project file content
            
        Returns:
            list: List of file paths
        """
        filelist = []
        split_content = content.split('\n')
        
        for line in split_content:
            if not line:
                continue
                
            parts = line.split('=', 1)
            if len(parts) < 2:
                continue
                
            key, value = parts
                
            if key == "file":
                filelist.append(value)
                self.nroffiles += 1
            elif key == "root":
                filelist.insert(0, value)
                self.rootfile = value
                
        return filelist
    
    def load_files(self, projfile, content):
        """Load all files in the project
        
        Args:
            projfile: Path to the project file
            content: Project file content
            
        Returns:
            bool: True if the files were loaded successfully
        """
        status = False
        rootpos = 0
        
        filelist = self.list_files(content)
        
        for i, filename in enumerate(filelist):
            if os.path.exists(filename):
                if not gummi.tabmanager.check_exists(filename):
                    gui.open_file(filename)
                    # Set the project file for the active editor
                    gui.active_editor.projfile = projfile
                
                status = True
                
            if i == 0:
                rootpos = gui.tabmanager.get_current_page()
        
        if status:
            gui.project.set_rootfile(rootpos)
        
        return status
    
    def get_value(self, content, item):
        """Get a value from the project file content
        
        Args:
            content: Project file content
            item: Key to look for
            
        Returns:
            str: Value for the given key
        """
        split_content = content.split('\n')
        
        for line in split_content:
            if not line:
                continue
                
            parts = line.split('=', 1)
            if len(parts) < 2:
                continue
                
            key, value = parts
            
            if key == item:
                return value
        
        return ""