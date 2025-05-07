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
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Project Manager

This module provides project management functionality for SilkTex.
"""

import os
import gi

gi.require_version('Gtk', '4.0')
from gi.repository import Gtk, Gio, GLib

class ProjectManager:
    """
    Project manager for SilkTex.
    
    This class provides project management functionality, including
    displaying files in the current project and opening files.
    """
    
    def __init__(self, window):
        """Initialize the project manager."""
        self.window = window
        self.current_directory = None
    
    def set_project_directory(self, directory):
        """Set the current project directory.
        
        Args:
            directory: The directory to set as the project root
        """
        if not os.path.isdir(directory):
            return False
            
        self.current_directory = directory
        return True
    
    def get_project_files(self):
        """Get a list of all files in the project.
        
        Returns:
            list: List of file paths in the project
        """
        if not self.current_directory:
            return []
            
        files = []
        
        for root, dirs, filenames in os.walk(self.current_directory):
            for filename in filenames:
                # Filter for relevant file types
                if self.is_relevant_file(filename):
                    files.append(os.path.join(root, filename))
                    
        return files
    
    def is_relevant_file(self, filename):
        """Check if a file is relevant to a LaTeX project.
        
        Args:
            filename: The filename to check
            
        Returns:
            bool: True if the file is relevant
        """
        # File extensions for relevant files
        relevant_extensions = [
            '.tex', '.bib', '.cls', '.sty', '.pdf', '.png', 
            '.jpg', '.jpeg', '.eps', '.svg', '.csv', '.txt'
        ]
        
        # Check if the file has a relevant extension
        _, ext = os.path.splitext(filename.lower())
        return ext in relevant_extensions
    
    def get_file_icon(self, filename):
        """Get an icon for a file based on its type.
        
        Args:
            filename: The filename
            
        Returns:
            str: Icon name for the file type
        """
        _, ext = os.path.splitext(filename.lower())
        
        # Map file extensions to icon names
        icon_map = {
            '.tex': 'text-x-tex-symbolic',
            '.bib': 'text-x-bibtex-symbolic',
            '.cls': 'text-x-tex-symbolic',
            '.sty': 'text-x-tex-symbolic',
            '.pdf': 'application-pdf-symbolic',
            '.png': 'image-x-generic-symbolic',
            '.jpg': 'image-x-generic-symbolic',
            '.jpeg': 'image-x-generic-symbolic',
            '.eps': 'image-x-generic-symbolic',
            '.svg': 'image-x-generic-symbolic',
            '.csv': 'x-office-spreadsheet-symbolic',
            '.txt': 'text-x-generic-symbolic'
        }
        
        return icon_map.get(ext, 'text-x-generic-symbolic')
    
    def build_project_tree(self):
        """Build a tree model for the project files.
        
        Returns:
            Gtk.TreeModel: Tree model of project files
        """
        # Create tree store with columns for:
        # - Display name
        # - File path
        # - Icon name
        tree_store = Gtk.TreeStore(str, str, str)
        
        if not self.current_directory:
            return tree_store
            
        # Add project files to the tree store
        self._add_directory_to_tree(tree_store, None, self.current_directory)
        
        return tree_store
    
    def _add_directory_to_tree(self, tree_store, parent_iter, directory):
        """Add a directory and its contents to the tree.
        
        Args:
            tree_store: The tree store to add to
            parent_iter: Parent iterator or None for root
            directory: The directory to add
        """
        # Get directory name
        dir_name = os.path.basename(directory)
        
        # Create a directory iter if this isn't the root
        if parent_iter is not None:
            dir_iter = tree_store.append(parent_iter, [dir_name, directory, 'folder-symbolic'])
        else:
            dir_iter = None
            
        # Get directory contents
        try:
            entries = os.listdir(directory)
            
            # Sort by type (directories first) and then by name
            dirs = sorted([e for e in entries if os.path.isdir(os.path.join(directory, e))])
            files = sorted([e for e in entries if os.path.isfile(os.path.join(directory, e)) and self.is_relevant_file(e)])
            
            # Add directories
            for dirname in dirs:
                # Skip hidden directories
                if dirname.startswith('.'):
                    continue
                    
                path = os.path.join(directory, dirname)
                self._add_directory_to_tree(tree_store, dir_iter or parent_iter, path)
            
            # Add files
            for filename in files:
                # Skip hidden files
                if filename.startswith('.'):
                    continue
                    
                path = os.path.join(directory, filename)
                icon_name = self.get_file_icon(filename)
                tree_store.append(dir_iter or parent_iter, [filename, path, icon_name])
                
        except OSError:
            # Handle directory access errors
            pass
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