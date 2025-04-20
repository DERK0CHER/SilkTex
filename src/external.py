#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file external.py
@brief Existence and compatibility checks for external tools

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
import shutil
from enum import Enum
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GLib

from . import constants
from . import utils

# Constants from constants.h
C_LATEX = "pdflatex"
C_RUBBER = "rubber"
C_LATEXMK = "latexmk"

# Define ExternalProg enum equivalent
class ExternalProg(Enum):
    EX_TEXLIVE = 0

# Local functions
def get_version_output(command, linenr):
    """Get version output from a command"""
    getversion = f"{command} --version"
    cmdgetv = utils.utils_popen_r(getversion, None)
    output = cmdgetv.second  # Assuming utils_popen_r returns a Tuple2 object
    result = "Unknown"

    if output is None:
        utils.slog("L_ERROR", f"Error detecting version for {command}. "
                             "Please report a bug\n")
        return result

    splitted = output.split("\n")
    if len(splitted) > linenr:
        result = splitted[linenr]
    return result

def version_latexmk(output):
    """Parse latexmk version from output"""
    # format: Latexmk, John Collins, 24 March 2011. Version 4.23a
    outarr = output.split(" ")
    version = outarr[-1]
    return version

def version_rubber(output):
    """Parse rubber version from output"""
    # format: Rubber version: 1.1
    outarr = output.split(" ")
    version = outarr[-1]
    return version

def get_texlive_version():
    """Get TeXLive version number"""
    version = 0
    output = get_version_output(C_LATEX, 0)

    # Keep in mind that some distros like themselves a lot:
    # pdfTeX 3.1415926-1.40.11-2.2 (TeX Live 2010)
    # pdfTeX 3.1415926-1.40.11-2.2 (TeX Live 2009/Debian)
    # pdfTeX 3.1415926-2.3-1.40.12 (TeX Live 2012/dev/Arch Linux)
    # pdfTeX 3.1415926-2.3-1.40.12 (Web2C 2011)
    #
    # Also, TeXLive utilities from versions before 2008 do not
    # mention the year in the --version tag.

    if (not utils.utils_subinstr("TeX Live", output, False) and
            not utils.utils_subinstr("Web2C", output, False)):
        return version

    splitted = output.split("(")
    segment = splitted[-1]
    segment = segment.replace("Web2C", "")
    resultstr = ""

    # Make sure to only allow numeric characters in the result
    for char in segment:
        if char.isdigit():
            resultstr += char

    try:
        version = float(resultstr)
    except ValueError:
        version = 0
        
    return version

def external_exists(program):
    """Check if an external program exists in PATH"""
    fullpath = shutil.which(program)
    if fullpath is None:
        return False
    
    return os.path.exists(fullpath)

def external_hasflag(program, flag):
    """Check if program supports a specific flag"""
    # Implementation always returns TRUE in original code
    return True

def external_version2(program):
    """Get numeric version of an external program"""
    if program == ExternalProg.EX_TEXLIVE:
        return get_texlive_version()
    else:
        return -1

def external_version(program):
    """Get string version of an external program"""
    version_cmd = f"{program} --version"
    cmdgetv = utils.utils_popen_r(version_cmd, None)
    version_output = cmdgetv.second  # Assuming utils_popen_r returns a Tuple2 object

    if version_output is None or version_output == "":
        return "Unknown, please report a bug"
    else:
        result = version_output.strip()

    # pdfTeX 3.1415926-1.40.10 (TeX Live 2009)
    # pdfTeX 3.1415926-1.40.11-2.2 (TeX Live 2010)
    # pdfTeX 3.1415926-2.3-1.40.12 (TeX Live 2011)
    # This is LuaTeX, Version 1.10.0 (TeX Live 2019/Debian)
    
    if program == C_RUBBER:
        result = version_rubber(result)
    elif program == C_LATEXMK:
        result = version_latexmk(result)

    return result