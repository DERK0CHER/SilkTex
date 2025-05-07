#!/usr/bin/env python3
"""
utils.py - Utility functions for Gummi

Copyright (C) 2025 Gummi Developers
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
import sys
import shutil
import subprocess
import threading
import tempfile
import gettext
from dataclasses import dataclass
from typing import Any, Optional, Tuple, List, Dict

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('Pango', '1.0')
from gi.repository import Gtk, Adw, Gio, GLib, Pango

# From constants.h
DIR_PERMS = 0o755
C_DIRSEP = os.path.sep

# Define log levels similar to the C version
L_INFO = 1
L_WARNING = 2
L_ERROR = 4
L_DEBUG = 8
L_FATAL = 16
L_G_INFO = 32
L_G_ERROR = 64
L_G_FATAL = 128

# Helper for checking log types
def L_IS_TYPE(level, type_flag):
    return level & type_flag != 0

# Helper for checking if it's a GUI log
def L_IS_GUI(level):
    return level & (L_G_INFO | L_G_ERROR | L_G_FATAL) != 0

# Color codes for terminal output
if sys.platform == 'win32':
    # Windows doesn't support ANSI color codes in regular console
    slogmsg_info = "[Info] "
    slogmsg_thread = "[Thread]"
    slogmsg_debug = "[Debug] "
    slogmsg_fatal = "[Fatal] "
    slogmsg_error = "[Error] "
    slogmsg_warning = "[Warning] "
else:
    # Use color codes for Unix-like systems
    slogmsg_info = "\033[1;34m[Info]\033[0m "
    slogmsg_thread = "\033[1;31m[Thread]\033[0m"
    slogmsg_debug = "\033[1;32m[Debug]\033[0m "
    slogmsg_fatal = "\033[1;37;41m[Fatal]\033[0m "
    slogmsg_error = "\033[1;31m[Error]\033[0m "
    slogmsg_warning = "\033[1;33m[Warning]\033[0m "

# Global variables similar to C version
slog_debug = 0
parent_window = None
main_thread = None
typesetter_pid = None

# Gettext helper for i18n (equivalent to _() in C)
_ = gettext.gettext

@dataclass
class Tuple2:
    """Equivalent to the C Tuple2 struct"""
    first: Any = None
    second: Any = None
    third: Any = None


def slog_init(debug):
    """Initialize logging system"""
    global slog_debug, main_thread
    slog_debug = debug
    main_thread = threading.current_thread()


def in_debug_mode():
    """Check if debug mode is enabled"""
    return slog_debug


def slog_set_gui_parent(window):
    """Set parent window for GUI dialogs"""
    global parent_window
    parent_window = window


def slog(level, message, *args):
    """Log a message with the specified level"""
    global main_thread

    # Format message if args are provided
    if args:
        message = message % args

    # Skip debug messages if not in debug mode
    if L_IS_TYPE(level, L_DEBUG) and not slog_debug:
        return

    # Show thread indicator if not in main thread
    if threading.current_thread() != main_thread:
        sys.stderr.write(slogmsg_thread)

    # Show appropriate level prefix
    if L_IS_TYPE(level, L_DEBUG):
        sys.stderr.write(slogmsg_debug)
    elif L_IS_TYPE(level, L_FATAL) or L_IS_TYPE(level, L_G_FATAL):
        sys.stderr.write(slogmsg_fatal)
    elif L_IS_TYPE(level, L_ERROR) or L_IS_TYPE(level, L_G_ERROR):
        sys.stderr.write(slogmsg_error)
    elif L_IS_TYPE(level, L_WARNING):
        sys.stderr.write(slogmsg_warning)
    else:
        sys.stderr.write(slogmsg_info)

    # Write message to stderr
    sys.stderr.write(message)
    sys.stderr.flush()

    # Show GUI dialog if needed
    if L_IS_GUI(level):
        dialog = Gtk.AlertDialog()
        dialog.set_modal(True)
        
        if L_IS_TYPE(level, L_G_ERROR):
            dialog.set_title("Error!")
        elif L_IS_TYPE(level, L_G_FATAL):
            dialog.set_title("Fatal Error!")
        elif L_IS_TYPE(level, L_G_INFO):
            dialog.set_title("Info")
            
        dialog.set_message(message)
        dialog.set_buttons(["OK"])
        dialog.present(parent_window)

    # Exit on fatal errors
    if not (L_IS_TYPE(level, L_INFO) or 
            L_IS_TYPE(level, L_DEBUG) or 
            L_IS_TYPE(level, L_ERROR) or 
            L_IS_TYPE(level, L_G_INFO) or 
            L_IS_TYPE(level, L_G_ERROR)):
        sys.exit(1)


def utils_save_reload_dialog(message):
    """Show save/reload dialog"""
    if not message:
        return 0
        
    dialog = Gtk.AlertDialog()
    dialog.set_message(message)
    dialog.set_title(_("Confirmation"))
    dialog.set_buttons(["Reload", "Save"])
    dialog.set_cancel_button(0)  # Set "Reload" as cancel button
    dialog.set_default_button(1)  # Set "Save" as default button
    
    result = dialog.choose(parent_window)
    
    # Return GTK_RESPONSE_YES for Reload, GTK_RESPONSE_NO for Save
    if result == 0:
        return Gtk.ResponseType.YES
    else:
        return Gtk.ResponseType.NO


def css_add(base, property_name, value):
    """Helper to create CSS property pairs"""
    return f"{base}{property_name}: {value}; "


def utils_pango_font_desc_to_css(font_desc):
    """Convert Pango font description to CSS"""
    if not isinstance(font_desc, Pango.FontDescription):
        return None
        
    font_mask = font_desc.get_set_fields()
    
    # Add selector
    result = "* { "
    
    # Add font family
    if font_mask & Pango.FontMask.FAMILY:
        result = css_add(result, "font-family", font_desc.get_family())
    
    # Add font style
    if font_mask & Pango.FontMask.STYLE:
        style = font_desc.get_style()
        if style == Pango.Style.NORMAL:
            val = "normal"
        elif style == Pango.Style.OBLIQUE:
            val = "oblique"
        elif style == Pango.Style.ITALIC:
            val = "italic"
        else:
            val = "normal"
        result = css_add(result, "font-style", val)
    
    # Add font variant
    if font_mask & Pango.FontMask.VARIANT:
        variant = font_desc.get_variant()
        if variant == Pango.Variant.NORMAL:
            val = "normal"
        elif variant == Pango.Variant.SMALL_CAPS:
            val = "small-caps"
        elif variant == Pango.Variant.ALL_SMALL_CAPS:
            val = "all-small-caps"
        elif variant == Pango.Variant.PETITE_CAPS:
            val = "petite-caps"
        elif variant == Pango.Variant.ALL_PETITE_CAPS:
            val = "all-petite-caps"
        elif variant == Pango.Variant.UNICASE:
            val = "unicase"
        elif variant == Pango.Variant.TITLE_CAPS:
            val = "title-caps"
        else:
            val = "normal"
        result = css_add(result, "font-variant", val)
    
    # Add font weight
    if font_mask & Pango.FontMask.WEIGHT:
        weight = font_desc.get_weight()
        result = css_add(result, "font-weight", str(weight))
    
    # Add font stretch
    if font_mask & Pango.FontMask.STRETCH:
        stretch = font_desc.get_stretch()
        if stretch == Pango.Stretch.ULTRA_CONDENSED:
            val = "ultra-condensed"
        elif stretch == Pango.Stretch.EXTRA_CONDENSED:
            val = "extra-condensed"
        elif stretch == Pango.Stretch.CONDENSED:
            val = "condensed"
        elif stretch == Pango.Stretch.SEMI_CONDENSED:
            val = "semi-condensed"
        elif stretch == Pango.Stretch.NORMAL:
            val = "normal"
        elif stretch == Pango.Stretch.SEMI_EXPANDED:
            val = "semi-expanded"
        elif stretch == Pango.Stretch.EXPANDED:
            val = "expanded"
        elif stretch == Pango.Stretch.EXTRA_EXPANDED:
            val = "extra-expanded"
        elif stretch == Pango.Stretch.ULTRA_EXPANDED:
            val = "ultra-expanded"
        else:
            val = "normal"
        result = css_add(result, "font-stretch", val)
    
    # Add font size
    if font_mask & Pango.FontMask.SIZE:
        size = font_desc.get_size()
        if not font_desc.get_size_is_absolute():
            size = size / Pango.SCALE
        result = css_add(result, "font-size", f"{size}px")
    
    # Add closing bracket
    result += "}"
    
    return result


def utils_yes_no_dialog(message):
    """Show a Yes/No dialog"""
    if not message:
        return 0
        
    dialog = Gtk.AlertDialog()
    dialog.set_message(message)
    dialog.set_title(_("Confirmation"))
    dialog.set_buttons(["Yes", "No"])
    dialog.set_cancel_button(1)  # Set "No" as cancel button
    dialog.set_default_button(0)  # Set "Yes" as default button
    
    result = dialog.choose(parent_window)
    
    # Return GTK_RESPONSE_YES or GTK_RESPONSE_NO
    if result == 0:
        return Gtk.ResponseType.YES
    else:
        return Gtk.ResponseType.NO


def utils_path_exists(path):
    """Check if a path exists"""
    if not path:
        return False
    
    file = Gio.File.new_for_path(path)
    return file.query_exists(None)


def utils_uri_path_exists(uri):
    """Check if a URI path exists"""
    if not uri:
        return False
    
    try:
        filepath = Gio.File.new_for_uri(uri).get_path()
        return utils_path_exists(filepath)
    except Exception:
        return False


def utils_set_file_contents(filename, text, length=None):
    """Set the contents of a file"""
    try:
        # Convert text to bytes if it's a string
        if isinstance(text, str):
            content = text.encode('utf-8')
        else:
            content = text
            
        # Use specific length if provided
        if length is not None:
            content = content[:length]
            
        file = Gio.File.new_for_path(filename)
        
        # Create parent directories if they don't exist
        parent = file.get_parent()
        if parent and not parent.query_exists(None):
            parent.make_directory_with_parents(None)
            
        # Write content to file
        if file.replace_contents(content, None, False, 
                                Gio.FileCreateFlags.NONE, 
                                None)[0]:
            return True
        return False
    except Exception as e:
        slog(L_ERROR, f"Failed to write file: {str(e)}\n")
        return False


def utils_copy_file(source, dest, error=None):
    """Copy a file from source to destination"""
    if not source or not dest:
        return False
        
    try:
        with open(source, 'rb') as f:
            contents = f.read()
        
        return utils_set_file_contents(dest, contents)
    except Exception as e:
        if error is not None:
            error[0] = str(e)
        return False


def utils_popen_r(cmd, chdir=None):
    """Execute command and return output and status"""
    global typesetter_pid
    
    if not cmd:
        return Tuple2()
        
    try:
        # Split command into args if it's a string
        if isinstance(cmd, str):
            args = cmd.split()
        else:
            args = cmd
            
        # Create process
        process = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            cwd=chdir,
            universal_newlines=True,
            text=True
        )
        
        # Store process ID for typesetter
        typesetter_pid = process.pid
        
        # Get output
        output, _ = process.communicate()
        status = process.returncode
        
        # Ensure output is UTF-8
        if output and not isinstance(output, str):
            try:
                output = output.decode('utf-8')
            except UnicodeDecodeError:
                output = output.decode('iso-8859-1')
                
        return Tuple2(None, status, output)
    except Exception as e:
        slog(L_G_FATAL, f"Command execution failed: {str(e)}")
        return Tuple2()


def utils_path_to_relative(root, target):
    """Convert absolute path to relative path"""
    if root and target and target.startswith(root):
        return target[len(root)+1:]
    return target


def utils_get_tmp_tmp_dir():
    """Get temporary directory in user's home"""
    tmp_tmp = os.path.join(GLib.get_home_dir(), "gtmp")
    os.makedirs(tmp_tmp, mode=DIR_PERMS, exist_ok=True)
    return tmp_tmp


def utils_glist_is_member(item_list, item):
    """Check if an item is a member of a list"""
    return item in item_list


def utils_subinstr(substr, target, case_insens=False):
    """Check if substring is in target string"""
    if not target or not substr:
        return False
        
    if case_insens:
        return substr.upper() in target.upper()
    else:
        return substr in target


def g_substr(src, start, end):
    """Get substring from start to end indices"""
    if not src:
        return None
    return src[start:end]


class SList:
    """Implementation of C slist structure"""
    def __init__(self, first=None, second=None, next_node=None):
        self.first = first
        self.second = second
        self.next = next_node


def slist_find(head, term, n=False, create=False):
    """Find an item in a linked list"""
    current = head
    prev = None
    
    while current:
        if n:
            if current.first and term and current.first.startswith(term):
                return current
        else:
            if current.first == term:
                return current
        prev = current
        current = current.next
        
    if create and prev:
        current = SList(first=term, second="")
        prev.next = current
        return current
        
    return None


def slist_append(head, node):
    """Append a node to a linked list"""
    if not head:
        return node
        
    current = head
    while current.next:
        current = current.next
        
    current.next = node
    return head


def slist_remove(head, node):
    """Remove a node from a linked list"""
    if not head or not node:
        return head
        
    if head == node:
        return head.next
        
    current = head
    while current.next:
        if current.next == node:
            current.next = node.next
            break
        current = current.next
        
    return head