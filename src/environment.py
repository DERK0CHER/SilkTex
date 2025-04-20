#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file environment.py
@brief Environment module for Gummi application

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

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource

# Import local modules (equivalent to includes in C)
from . import configfile
from .gui import gui_main
from . import utils
from .editor import editor

# Global variables (similar to the C code)
gummi = None
gui = None
g_active_editor = None

class Gummi:
    """Main Gummi application class"""
    def __init__(self, motion, io, latex, biblio, templ, snippets, tabmanager, project):
        """Initialize the Gummi application object"""
        self.io = io
        self.motion = motion
        self.latex = latex
        self.biblio = biblio
        self.templ = templ
        self.snippets = snippets
        self.tabmanager = tabmanager
        self.project = project

def gummi_init(motion, io, latex, biblio, templ, snippets, tabmanager, project):
    """Initialize the global Gummi instance"""
    global gummi
    gummi = Gummi(motion, io, latex, biblio, templ, snippets, tabmanager, project)
    return gummi

def gummi_project_active():
    """Check if a project is currently active"""
    if gummi.project.projfile:
        return True
    return False

def gummi_get_projectfile():
    """Get the current project file path"""
    return gummi.project.projfile

def gummi_new_environment(filename):
    """Create a new editing environment for a file"""
    ec = editor.editor_new(gummi.motion)
    editor.editor_fileinfo_update(ec, filename)
    
    utils.slog("L_INFO", "\n")
    utils.slog("L_INFO", "Environment created for:\n")
    utils.slog("L_INFO", f"TEX: {ec.filename}\n")
    utils.slog("L_INFO", f"TMP: {ec.workfile}\n")
    utils.slog("L_INFO", f"PDF: {ec.pdffile}\n")
    
    return ec

def gummi_get_gui():
    """Get the global GUI instance"""
    global gui
    return gui

def gummi_get_active_editor():
    """Get the currently active editor"""
    global g_active_editor
    return g_active_editor

def gummi_get_all_tabs():
    """Get all tabs in the application"""
    return gummi.tabmanager.tabs

def gummi_get_all_editors():
    """Get all editor instances from all tabs"""
    editors = []
    tabs = gummi_get_all_tabs()
    
    for tab in tabs:
        ec = tab.editor  # Equivalent to GU_TAB_CONTEXT macro in C
        editors.append(ec)
    
    return editors

def gummi_get_io():
    """Get the IO functionality object"""
    return gummi.io

def gummi_get_motion():
    """Get the motion functionality object"""
    return gummi.motion

def gummi_get_latex():
    """Get the LaTeX functionality object"""
    return gummi.latex

def gummi_get_biblio():
    """Get the bibliography functionality object"""
    return gummi.biblio

def gummi_get_template():
    """Get the template functionality object"""
    return gummi.templ

def gummi_get_snippets():
    """Get the snippets functionality object"""
    return gummi.snippets