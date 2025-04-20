#!/usr/bin/env python3
# **
# * **@file** latexmk.py
# * **@brief** Python implementation of latexmk.c
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

import os
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource

from configfile import config_value_as_str_equals, config_get_boolean
from constants import C_LATEXMK, C_TMPDIR
from external import external_exists, external_version
from utils import slog, L_INFO, STR_EQU

# Global variable to track if latexmk is detected
lmk_detected = False

def latexmk_init():
    """Initialize latexmk typesetter detection."""
    global lmk_detected
    
    if external_exists(C_LATEXMK):
        # TODO: check if supported version
        slog(L_INFO, f"Typesetter detected: Latexmk {external_version(C_LATEXMK)}")
        lmk_detected = True

def latexmk_active():
    """Check if latexmk is the active typesetter in config."""
    if config_value_as_str_equals("Compile", "typesetter", C_LATEXMK):
        return True
    return False

def latexmk_detected():
    """Check if latexmk was detected during initialization."""
    return lmk_detected

def latexmk_get_command(method, workfile, basename):
    """
    Generate the latexmk command with appropriate flags.
    
    Args:
        method: Compilation method (e.g., "texpdf")
        workfile: Path to the LaTeX file to compile
        basename: Base name of the output file
        
    Returns:
        String containing the full latexmk command
    """
    outdir = ""
    
    # reroute output files to our temp directory
    if not STR_EQU(C_TMPDIR, os.path.dirname(workfile)):
        base = os.path.basename(basename)
        outdir = f'-jobname="{C_TMPDIR}/{base}"'
    
    flags = latexmk_get_flags(method)
    lmkcmd = f'latexmk {flags} {outdir} "{workfile}"'
    return lmkcmd

def latexmk_get_flags(method):
    """
    Get latexmk flags based on the compilation method.
    
    Args:
        method: Compilation method (e.g., "texpdf")
        
    Returns:
        String containing latexmk command flags
    """
    if config_get_boolean("Compile", "synctex"):
        if STR_EQU(method, "texpdf"):
            lmkflags = r'-e "\$pdflatex = \'pdflatex -synctex=1\'" -silent'
        else:
            lmkflags = r'-e "\$latex = \'latex -synctex=1\'" -silent'
    else:
        if STR_EQU(method, "texpdf"):
            lmkflags = r'-e "\$pdflatex = \'pdflatex -synctex=0\'" -silent'
        else:
            lmkflags = r'-e "\$latex = \'latex -synctex=0\'" -silent'
    
    if STR_EQU(method, "texpdf"):
        lmkwithoutput = f"{lmkflags} -pdf"
    elif STR_EQU(method, "texdvipdf"):
        lmkwithoutput = f"{lmkflags} -pdfdvi"
    else:
        lmkwithoutput = f"{lmkflags} -pdfps"
    
    return lmkwithoutput