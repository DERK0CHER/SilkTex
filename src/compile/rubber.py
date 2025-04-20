#!/usr/bin/env python3
# **
# * **@file** rubber.py
# * **@brief** Python implementation of rubber.c
# *
# * Copyright (C) 2009 Gummi Developers
# * All Rights reserved.
# *
# * Permission is hereby granted, free of charge, to any person
# * obtaining a copy of this software and associated documentation
# * files (the "Software"), to deal in the Software without
# * restriction, including without limitation the rights to use,
# * copy, modify, merge, publish, distribute, sublicense, and/or sell
# * copies of the Software, and to permit persons to whom the
# * Software is furnished to do so, subject to the following
# * conditions:
# *
# * The above copyright notice and this permission notice shall be
# * included in all copies or substantial portions of the Software.
# *
# * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
# * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# * OTHER DEALINGS IN THE SOFTWARE.
# */

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource

from configfile import config_value_as_str_equals, config_get_boolean
from constants import C_RUBBER, C_TMPDIR
from external import external_exists, external_version
from utils import slog, L_INFO, STR_EQU

# Global variable to track if rubber is detected
rub_detected = False

def rubber_init():
    """Initialize rubber typesetter detection."""
    global rub_detected
    
    if external_exists(C_RUBBER):
        # TODO: check if supported version
        slog(L_INFO, f"Typesetter detected: Rubber {external_version(C_RUBBER)}")
        rub_detected = True

def rubber_active():
    """Check if rubber is the active typesetter in config."""
    if config_value_as_str_equals("Compile", "typesetter", C_RUBBER):
        return True
    return False

def rubber_detected():
    """Check if rubber was detected during initialization."""
    return rub_detected

def rubber_get_command(method, workfile):
    """
    Generate the rubber command with appropriate flags.
    
    Args:
        method: Compilation method (e.g., "texpdf")
        workfile: Path to the LaTeX file to compile
        
    Returns:
        String containing the full rubber command
    """
    outdir = f'--into="{C_TMPDIR}"'
    flags = rubber_get_flags(method)
    rubcmd = f'rubber {flags} {outdir} "{workfile}"'
    return rubcmd

def rubber_get_flags(method):
    """
    Get rubber flags based on the compilation method.
    
    Args:
        method: Compilation method (e.g., "texpdf")
        
    Returns:
        String containing rubber command flags
    """
    if STR_EQU(method, "texpdf"):
        rubflags = "-d -q"
    else:
        rubflags = "-p -d -q"
    
    if config_get_boolean("Compile", "synctex"):
        rubflags = f"--synctex {rubflags}"
    
    return rubflags