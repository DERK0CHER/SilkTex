#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file configfile.py
@brief Handle configuration file

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
import hashlib
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GLib

from . import constants
from . import utils

# Constants (from constants.h)
C_PACKAGE_VERSION = "0.8.0"  # Replace with actual version
C_GUMMI_CONFDIR = os.path.expanduser("~/.config/gummi")
C_GUMMI_TEMPLATEDIR = os.path.join(C_GUMMI_CONFDIR, "templates")
C_WELCOMETEXT = os.path.join(C_GUMMI_CONFDIR, "welcome.tex")
C_DEFAULTTEXT = "/usr/share/gummi/default.tex"  # Replace with actual path
DIR_PERMS = 0o755

# Default configuration
default_config = f"""[General]
config_version = {C_PACKAGE_VERSION}

[Interface]
mainwindow_x = 0
mainwindow_y = 0
mainwindow_w = 792
mainwindow_h = 558
mainwindow_max = false
toolbar = true
statusbar = true
rightpane = true
snippets = true
[Editor]
font_str = Monospace 14
font_css = * {{ font-family: Monospace; font-size: 14px; }}
line_numbers = true
highlighting = true
textwrapping = true
wordwrapping = true
tabwidth = 4
spaces_instof_tabs = false
autoindentation = true
style_scheme = classic
spelling = false
spelling_lang = None

[Preview]
zoom_mode = Fit Page Width
pagelayout = one_column
autosync = false
animated_scroll = always
cache_size = 150

[File]
autosaving = false
autosave_timer = 10
autoexport = false

[Compile]
typesetter = pdflatex
steps = texpdf
pause = false
scheme = on_idle
timer = 1
shellescape = true
synctex = false

[Misc]
recent1 = __NULL__
recent2 = __NULL__
recent3 = __NULL__
recent4 = __NULL__
recent5 = __NULL__"""

# Global variables
key_file = None
conf_filepath = None


def config_init():
    """Initialize configuration system"""
    global key_file, conf_filepath
    
    conf_filepath = os.path.join(C_GUMMI_CONFDIR, "gummi.ini")
    
    # Create config & template dirs if they don't exist
    if not os.path.isdir(C_GUMMI_TEMPLATEDIR):
        utils.slog("L_WARNING", "Template directory does not exist, creating..\n")
        os.makedirs(C_GUMMI_TEMPLATEDIR, mode=DIR_PERMS, exist_ok=True)
    
    # Load config file
    key_file = GLib.KeyFile()
    
    try:
        if not key_file.load_from_file(conf_filepath, GLib.KeyFileFlags.NONE):
            utils.slog("L_WARNING", "Unable to load config, resetting defaults\n")
            config_load_defaults(key_file)
    except GLib.Error as error:
        if error.matches(GLib.FileError.quark(), GLib.FileError.NOENT):
            utils.slog("L_WARNING", "Unable to load config, resetting defaults\n")
        else:
            utils.slog("L_ERROR", f"{error.message}\n")
        config_load_defaults(key_file)
    
    # Replace old welcome texts if still active
    if os.path.exists(C_WELCOMETEXT):
        with open(C_WELCOMETEXT, 'r') as f:
            text = f.read()
            # Calculate string hash similar to g_str_hash
            hash_value = hashlib.md5(text.encode()).hexdigest()
            
            # Check against known hash values (modify these to match actual values)
            if hash_value in ["a1b2c3d4", "e5f6g7h8", "i9j0k1l2"]:  # Placeholder hashes
                utils.slog("L_WARNING", "Replacing unchanged welcome text with new default\n")
                utils.utils_copy_file(C_DEFAULTTEXT, C_WELCOMETEXT, None)
    
    utils.slog("L_INFO", f"Configuration file: {conf_filepath}\n")


def config_get_string(group, key):
    """Get string value from config"""
    try:
        value = key_file.get_string(group, key)
        return value
    except GLib.Error:
        return config_get_default_string(group, key)


def config_get_boolean(group, key):
    """Get boolean value from config"""
    try:
        value = key_file.get_boolean(group, key)
        return value
    except GLib.Error:
        return config_get_default_boolean(group, key)


def config_get_integer(group, key):
    """Get integer value from config"""
    try:
        value = key_file.get_integer(group, key)
        return value
    except GLib.Error:
        return config_get_default_integer(group, key)


def config_get_default_string(group, key):
    """Get default string value and set it in config"""
    default_keys = GLib.KeyFile()
    default_keys.load_from_data(default_config, len(default_config), GLib.KeyFileFlags.NONE)
    
    utils.slog("L_WARNING", f"Config get default value for '{group}.{key}'\n")
    
    default_value = default_keys.get_string(group, key)
    config_set_string(group, key, default_value)
    
    return default_value


def config_get_default_boolean(group, key):
    """Get default boolean value and set it in config"""
    default_keys = GLib.KeyFile()
    default_keys.load_from_data(default_config, len(default_config), GLib.KeyFileFlags.NONE)
    
    utils.slog("L_WARNING", f"Config get default value for '{group}.{key}'\n")
    
    default_value = default_keys.get_boolean(group, key)
    config_set_boolean(group, key, default_value)
    
    return default_value


def config_get_default_integer(group, key):
    """Get default integer value and set it in config"""
    default_keys = GLib.KeyFile()
    default_keys.load_from_data(default_config, len(default_config), GLib.KeyFileFlags.NONE)
    
    utils.slog("L_WARNING", f"Config get default value for '{group}.{key}'\n")
    
    default_value = default_keys.get_integer(group, key)
    config_set_integer(group, key, default_value)
    
    return default_value


def config_value_as_str_equals(group, key, input_val):
    """Check if config value equals provided string"""
    value = config_get_string(group, key)
    if value == input_val:
        return True
    return False


def config_set_string(group, key, value):
    """Set string value in config"""
    key_file.set_string(group, key, value)


def config_set_boolean(group, key, value):
    """Set boolean value in config"""
    key_file.set_boolean(group, key, value)


def config_set_integer(group, key, value):
    """Set integer value in config"""
    key_file.set_integer(group, key, value)


def config_load_defaults(key_file):
    """Load default configuration"""
    try:
        key_file.load_from_data(default_config, len(default_config), GLib.KeyFileFlags.NONE)
    except GLib.Error as error:
        utils.slog("L_ERROR", f"Error loading default config: {error.message}\n")
    
    config_save()


def config_save():
    """Save configuration to file"""
    try:
        if not key_file.save_to_file(conf_filepath):
            utils.slog("L_ERROR", "Error saving config: Unknown error\n")
    except GLib.Error as error:
        utils.slog("L_ERROR", f"Error saving config: {error.message}\n")