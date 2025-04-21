#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
@file   iofunctions.py
@brief  File IO operations for Gummi

Copyright (C) 2009 Gummi Developers
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

import os
import locale
from typing import Optional, List, Any

import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gtk, GLib, GObject

# Import other modules (these would be defined elsewhere in the application)
from constants import C_WELCOMETEXT, C_DEFAULTTEXT, C_DIRSEP
from configfile import config_get_integer, config_get_boolean
from utils import copy_file, path_exists
import logging

# Global reference to Gummi application
gummi = None

class SaveContext(GObject.GObject):
    """Context for saving file operation"""
    def __init__(self):
        GObject.GObject.__init__(self)
        self.filename = None
        self.text = None

class IOFunctions:
    """Class to handle file I/O operations"""
    
    def __init__(self):
        """Initialize IO Functions handler"""
        self.sig_hook = GObject.Object()
        self.autosave_id = 0
        
        # Connect signals
        self.sig_hook.connect("document-load", self._real_load_file)
        self.sig_hook.connect("document-write", self._real_save_file)
    
    def load_default_text(self, looped_once: bool = False):
        """Load default welcome text"""
        try:
            with open(C_WELCOMETEXT, 'r', encoding='utf-8') as f:
                text = f.read()
        except Exception as e:
            logging.warning(f"Could not find default welcome text, resetting: {e}")
            try:
                copy_file(C_DEFAULTTEXT, C_WELCOMETEXT)
                if not looped_once:
                    return self.load_default_text(True)
            except Exception as e:
                logging.error(f"Failed to copy default text: {e}")
                return
        
        # Fill editor buffer with text
        ec = gummi.get_active_editor()
        if ec and text:
            ec.fill_buffer(text)
            ec.buffer.set_modified(False)
    
    def load_file(self, filename: str):
        """Load a file into the editor"""
        logging.info(f"Loading {filename}...")
        
        # Add loading message to status bar
        status = f"Loading {filename}..."
        gummi.gui.statusbar.set_message(status)
        
        # Emit signal to load document
        self.sig_hook.emit("document-load", filename)
    
    def _real_load_file(self, hook: GObject.Object, filename: str):
        """Actually load the file content - signal callback"""
        try:
            with open(filename, 'r', encoding='utf-8') as f:
                text = f.read()
        except UnicodeDecodeError:
            # Try to decode with different encoding
            decoded = self._decode_text(self._read_file_raw(filename))
            if decoded is None:
                self.load_default_text(False)
                return
            text = decoded
        except Exception as e:
            logging.error(f"Failed to read file: {e}")
            self.load_default_text(False)
            return
        
        # Fill editor buffer with text
        ec = gummi.get_active_editor()
        if ec:
            ec.fill_buffer(text)
            ec.buffer.set_modified(False)
    
    def _read_file_raw(self, filename: str) -> Optional[bytes]:
        """Read file in binary mode"""
        try:
            with open(filename, 'rb') as f:
                return f.read()
        except Exception as e:
            logging.error(f"Failed to read file in binary mode: {e}")
            return None
    
    def save_file(self, filename: str, text: str):
        """Save text to a file"""
        status = f"Saving {filename}..."
        gummi.gui.statusbar.set_message(status)
        
        # Create save context
        savecontext = SaveContext()
        savecontext.filename = filename
        savecontext.text = text
        
        # Emit signal to save document
        self.sig_hook.emit("document-write", savecontext)
        
        # Mark buffer as not modified
        ec = gummi.get_active_editor()
        if ec:
            ec.buffer.set_modified(False)
    
    def _real_save_file(self, hook: GObject.Object, savecontext: SaveContext):
        """Actually save the file content - signal callback"""
        filename = savecontext.filename
        text = savecontext.text
        
        if not filename or not text:
            return
        
        # Encode text for saving
        encoded = self._encode_text(text)
        
        try:
            with open(filename, 'w', encoding='utf-8') as f:
                f.write(encoded)
        except Exception as e:
            logging.error(f"Failed to save file: {e}")
    
    def get_swapfile(self, filename: str) -> str:
        """Get the path to the swap file for a given file"""
        if not filename:
            return ""
            
        basename = os.path.basename(filename)
        dirname = os.path.dirname(filename)
        swapfile = f"{dirname}{C_DIRSEP}.{basename}.swp"
        
        return swapfile
    
    def has_swapfile(self, filename: str) -> bool:
        """Check if a swap file exists for the given file"""
        if not filename:
            return False
            
        swapfile = self.get_swapfile(filename)
        return path_exists(swapfile)
    
    def start_autosave(self):
        """Start the autosave timer"""
        # Convert minutes to milliseconds
        interval = config_get_integer("File", "autosave_timer") * 60 * 1000
        
        self.autosave_id = GLib.timeout_add(interval, self._autosave_cb, None)
        logging.debug("Autosaving function started...")
    
    def stop_autosave(self):
        """Stop the autosave timer"""
        if self.autosave_id > 0:
            GLib.source_remove(self.autosave_id)
            self.autosave_id = 0
            logging.debug("Autosaving function stopped...")
        else:
            logging.error("Error occurred stopping autosaving...")
    
    def reset_autosave(self, name: str):
        """Reset the autosave timer"""
        self.stop_autosave()
        if config_get_boolean("File", "autosaving"):
            self.start_autosave()
    
    def _decode_text(self, text_bytes: Optional[bytes]) -> Optional[str]:
        """Decode text from bytes to string using appropriate encoding"""
        if not text_bytes:
            return None
            
        # Try with system locale first
        try:
            return text_bytes.decode(locale.getpreferredencoding())
        except UnicodeDecodeError:
            logging.error("Failed to convert text from default locale, trying ISO-8859-1")
            
            # Try with ISO-8859-1
            try:
                return text_bytes.decode('iso-8859-1', errors='ignore')
            except UnicodeDecodeError:
                logging.error("Cannot convert text to UTF-8!")
                return None
    
    def _encode_text(self, text: str) -> str:
        """Encode text for saving (mostly a passthrough in Python)"""
        # In Python, the file will be written with the encoding specified in open()
        # This function is kept for API compatibility with C version
        return text
    
    def _autosave_cb(self, user_data: Any) -> bool:
        """Callback for autosave timer"""
        tabs = gummi.get_all_tabs()
        
        # Skip autosave if no tabs are open
        if not tabs:
            return True
        
        for tab in tabs:
            ec = tab.editor
            
            # Save only if the file has a name and has been modified
            if ec.filename and ec.buffer_changed():
                focus = gummi.gui.mainwindow.get_focus()
                text = ec.grab_buffer()
                
                if focus:
                    focus.grab_focus()
                    
                self.save_file(ec.filename, text)
                ec.buffer.set_modified(False)
                logging.debug(f"Autosaving document: {ec.filename}")
                gummi.gui.set_filename_display(tab, True, True)
        
        # Return True to keep the timer running
        return True


def iofunctions_init():
    """Initialize IOFunctions module (for compatibility with C-style code)"""
    return IOFunctions()