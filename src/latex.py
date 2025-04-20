#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
@file   latex.py
@brief  LaTeX compilation handler for Gummi

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
import re
import shutil
import subprocess
from typing import List, Optional, Tuple, Any

import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gtk, GLib

# Import local modules (these would be defined elsewhere in the application)
from constants import C_TEXSEC, C_TMPDIR, C_DIRSEP
from utils import (set_file_contents, popen_r, subinstr, 
                  path_exists, copy_file, yes_no_dialog)
from configfile import (config_get_string, config_get_boolean, 
                       config_set_string, config_value_as_str_equals)
from compile.rubber import rubber_init, rubber_active, rubber_get_command
from compile.latexmk import latexmk_init, latexmk_active, latexmk_get_command
from compile.texlive import texlive_init, texlive_get_command
import logging

# Global reference to the Gummi application instance
gummi = None

class LaTeX:
    """Class to handle LaTeX compilation and processing"""
    
    def __init__(self):
        """Initialize LaTeX handler"""
        self.compilelog = None
        self.modified_since_compile = False
        self.errorlines = [0] * 1024  # Equivalent to BUFSIZ in C
        
        # Initialize TeX compilers
        self.tex_version = texlive_init()
        rubber_init()
        latexmk_init()
    
    def method_active(self, method: str) -> bool:
        """Check if the specified compilation method is active"""
        return config_value_as_str_equals("Compile", "steps", method)
    
    def update_workfile(self, editor) -> str:
        """Update the working file with current editor content"""
        text = editor.grab_buffer()
        
        # Only write buffer content when there is not a recovery in progress
        if text != "":
            set_file_contents(editor.workfile, text, -1)
        
        return text
    
    def set_compile_cmd(self, editor) -> str:
        """Set the LaTeX compilation command based on current configuration"""
        method = config_get_string("Compile", "steps")
        texcmd = None
        
        if rubber_active():
            texcmd = rubber_get_command(method, editor.workfile)
        elif latexmk_active():
            texcmd = latexmk_get_command(method, editor.workfile, editor.basename)
        else:
            texcmd = texlive_get_command(method, editor.workfile, editor.basename)
        
        combined = f"{C_TEXSEC} {texcmd}"
        return combined
    
    def analyse_log(self, log: str, filename: Optional[str], basename: str) -> str:
        """Analyze the compilation log file"""
        # Rubber doesn't post pdftex output to tty, so read from log file
        if rubber_active():
            if filename is None:
                logpath = f"{basename}.log"
            else:
                logpath = f"{C_TMPDIR}{C_DIRSEP}{os.path.basename(basename)}.log"
            
            try:
                with open(logpath, 'r', encoding='utf-8') as f:
                    log = f.read()
            except Exception as e:
                logging.error(f"Error reading log file: {e}")
        
        return log
    
    def analyse_errors(self):
        """Analyze compilation errors from the log"""
        try:
            # Reset error lines
            self.errorlines = [0] * 1024
            
            if self.compilelog is None:
                logging.error("Compilation log is null")
                return
            
            # Match line numbers in error messages
            pattern = r":(\d+):"
            matches = re.finditer(pattern, self.compilelog)
            
            count = 0
            for match in matches:
                if count + 1 >= 1024:  # BUFSIZ equivalent
                    break
                self.errorlines[count] = int(match.group(1))
                count += 1
            
            if not self.errorlines[0]:
                self.errorlines[0] = -1
                
        except Exception as e:
            logging.error(f"Error analyzing errors: {e}")
    
    def update_pdffile(self, editor) -> bool:
        """Update the PDF file by compiling the LaTeX document"""
        basename = editor.basename
        filename = editor.filename
        
        if not self.modified_since_compile:
            # If nothing has changed, return previous compilation status
            return not bool(self.errorlines[0] > 0)
        
        typesetter = config_get_string("Compile", "typesetter")
        # Check if typesetter exists
        try:
            subprocess.run(['which', typesetter], capture_output=True, check=True)
        except subprocess.CalledProcessError:
            # Set to default typesetter
            config_set_string("Compile", "typesetter", "pdflatex")
        
        # Create compile command
        curdir = os.path.dirname(editor.workfile)
        command = self.set_compile_cmd(editor)
        
        # Reset error tracking
        self.compilelog = None
        self.errorlines = [0] * 1024
        
        # Run PDF compilation
        cerrors, coutput = popen_r(command, curdir)
        
        # Analyze compilation output
        self.compilelog = self.analyse_log(coutput, filename, basename)
        self.modified_since_compile = False
        
        # Find error lines if compilation failed
        if cerrors and self.compilelog and len(self.compilelog) > 0:
            self.analyse_errors()
        
        return cerrors == 0
    
    def update_auxfile(self, editor):
        """Update auxiliary files"""
        dirname = os.path.dirname(editor.workfile)
        typesetter = config_get_string("Compile", "typesetter")
        
        command = (f"{C_TEXSEC} {typesetter} "
                  f"--draftmode "
                  f"-interaction=nonstopmode "
                  f"--output-directory=\"{C_TMPDIR}\" \"{editor.workfile}\"")
        
        _, _ = popen_r(command, dirname)
    
    def remove_auxfile(self, editor) -> int:
        """Remove auxiliary files"""
        if editor.filename is None:
            auxfile = f"{editor.basename}.aux"
        else:
            auxfile = f"{C_TMPDIR}{C_DIRSEP}{os.path.basename(editor.basename)}.aux"
        
        # Remove the aux file if it exists
        try:
            if os.path.exists(auxfile):
                os.remove(auxfile)
                return 0
        except Exception as e:
            logging.error(f"Error removing auxfile: {e}")
            
        return -1
    
    def precompile_check(self, editortext: str) -> bool:
        """Check if the document has basic LaTeX structure"""
        has_class = subinstr("\\documentclass", editortext, False)
        has_style = subinstr("\\documentstyle", editortext, False)
        has_input = subinstr("\\input", editortext, False)
        
        return has_class or has_style or has_input
    
    def export_pdffile(self, editor, path: str, prompt_overwrite: bool):
        """Export the compiled PDF file to the specified path"""
        if not path.lower().endswith('.pdf'):
            savepath = f"{path}.pdf"
        else:
            savepath = path
        
        # Check if file exists and prompt for overwrite if needed
        if prompt_overwrite and path_exists(savepath):
            ret = yes_no_dialog("The file already exists. Overwrite?")
            if ret != Gtk.ResponseType.YES:
                return
        
        # Copy the PDF file
        try:
            copy_file(editor.pdffile, savepath)
        except Exception as e:
            logging.error(f"Unable to export PDF file: {e}")
    
    def run_makeindex(self, editor) -> bool:
        """Run makeindex on the document"""
        # Check if makeindex is available
        try:
            subprocess.run(['which', 'makeindex'], capture_output=True, check=True)
        except subprocess.CalledProcessError:
            return False
        
        # Run makeindex
        command = f"{C_TEXSEC} makeindex \"{os.path.basename(editor.basename)}.idx\""
        retcode, _ = popen_r(command, C_TMPDIR)
        
        return retcode == 0
    
    def can_synctex(self) -> bool:
        """Check if SyncTeX is available"""
        return self.tex_version >= 2008
    
    def use_synctex(self) -> bool:
        """Check if SyncTeX should be used"""
        return (config_get_boolean("Compile", "synctex") and
                config_get_boolean("Preview", "autosync"))
    
    def use_shellescaping(self) -> bool:
        """Check if shell escaping should be enabled"""
        return config_get_boolean("Compile", "shellescape")


def latex_init():
    """Initialize LaTeX module (for compatibility with C-style code)"""
    return LaTeX()