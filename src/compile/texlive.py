#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Texlive typesetter functionality for Gummi.

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

import os
from configfile import config_get_boolean, config_value_as_str_equals
from constants import C_LATEX, C_PDFLATEX, C_XELATEX, C_LUALATEX, C_TMPDIR, GUMMI_LIBS
from external import external_exists, external_version, external_version2, EX_TEXLIVE
from latex import latex_use_shellescaping
from utils import STR_EQU
from logger import slog, L_INFO

# Global detection flags
pdf_detected = False
xel_detected = False
lua_detected = False


def texlive_init():
    """Initialize Texlive and detect available typesetters.
    
    Returns:
        int: The detected Texlive version, or 0 if not found
    """
    global pdf_detected, xel_detected, lua_detected
    
    texversion = 0
    if external_exists(C_LATEX):
        texversion = external_version2(EX_TEXLIVE)
        slog(L_INFO, f"Texlive {texversion} was found installed")
    
    if external_exists(C_PDFLATEX):
        slog(L_INFO, f"Typesetter detected: {external_version(C_PDFLATEX)}")
        pdf_detected = True
    
    if external_exists(C_XELATEX):
        slog(L_INFO, f"Typesetter detected: {external_version(C_XELATEX)}")
        xel_detected = True
    
    if external_exists(C_LUALATEX):
        slog(L_INFO, f"Typesetter detected: {external_version(C_LUALATEX)}")
        lua_detected = True
    
    return texversion


def texlive_active():
    """Check if any Texlive typesetter is active.
    
    Returns:
        bool: True if any Texlive typesetter is active, False otherwise
    """
    if pdflatex_active() or xelatex_active() or lualatex_active():
        return True
    return False


def pdflatex_active():
    """Check if pdflatex is the active typesetter.
    
    Returns:
        bool: True if pdflatex is active, False otherwise
    """
    if config_value_as_str_equals("Compile", "typesetter", "pdflatex"):
        return True
    return False


def xelatex_active():
    """Check if xelatex is the active typesetter.
    
    Returns:
        bool: True if xelatex is active, False otherwise
    """
    if config_value_as_str_equals("Compile", "typesetter", "xelatex"):
        return True
    return False


def lualatex_active():
    """Check if lualatex is the active typesetter.
    
    Returns:
        bool: True if lualatex is active, False otherwise
    """
    if config_value_as_str_equals("Compile", "typesetter", "lualatex"):
        return True
    return False


def pdflatex_detected():
    """Check if pdflatex was detected on the system.
    
    Returns:
        bool: True if pdflatex was detected, False otherwise
    """
    return pdf_detected


def xelatex_detected():
    """Check if xelatex was detected on the system.
    
    Returns:
        bool: True if xelatex was detected, False otherwise
    """
    return xel_detected


def lualatex_detected():
    """Check if lualatex was detected on the system.
    
    Returns:
        bool: True if lualatex was detected, False otherwise
    """
    return lua_detected


def texlive_get_command(method, workfile, basename):
    """Get the command to compile LaTeX documents.
    
    Args:
        method: The compilation method (texpdf, texdvipdf, texdvipspdf)
        workfile: The LaTeX file to compile
        basename: The base name of the output file
        
    Returns:
        str: The command to execute
    """
    outdir = f'-output-directory="{C_TMPDIR}"'
    
    if pdflatex_active():
        typesetter = C_PDFLATEX
    elif lualatex_active():
        typesetter = C_LUALATEX
    else:
        typesetter = C_XELATEX
    
    flags = texlive_get_flags("texpdf")
    dviname = f"{os.path.basename(basename)}.dvi"
    psname = f"{os.path.basename(basename)}.ps"
    
    if os.name == 'nt':  # Windows
        script = os.path.join(GUMMI_LIBS, "latex_dvi.cmd")
    else:
        script = os.path.join(GUMMI_LIBS, "latex_dvi.sh")
    
    if STR_EQU(method, "texpdf"):
        texcmd = f'{typesetter} {flags} {outdir} "{workfile}"'
    elif STR_EQU(method, "texdvipdf"):
        texcmd = f'{script} pdf "{flags}" "{outdir}" "{workfile}" "{C_TMPDIR}" "{dviname}"'
    else:  # texdvipspdf
        texcmd = f'{script} ps "{flags}" "{outdir}" "{workfile}" "{C_TMPDIR}" "{dviname}" "{psname}"'
    
    return texcmd


def texlive_get_flags(method):
    """Get the flags for the LaTeX compilation command.
    
    Args:
        method: The compilation method (unused in this implementation)
        
    Returns:
        str: The flags to use
    """
    flags = "-interaction=nonstopmode -file-line-error -halt-on-error"
    
    if not latex_use_shellescaping():
        flags = f"{flags} -no-shell-escape"
    else:
        flags = f"{flags} -shell-escape"
    
    if config_get_boolean("Compile", "synctex"):
        flags = f"{flags} -synctex=1"
    
    return flags