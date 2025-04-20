#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file importer.py
@brief LaTeX code generator for tables, matrices and images

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
import platform
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GLib

from . import editor
from . import environment
from . import utils

# External variables
gummi = None

# Constants
ALIGN_TYPE = ["l", "c", "r"]
BRACKET_TYPE = ["matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix"]
BUFFER_SIZE = 2048  # equivalent to BUFSIZ * 2 in C

def importer_generate_table(rows, cols, borders, alignment):
    """
    Generate LaTeX code for a table.
    
    Args:
        rows: Number of rows
        cols: Number of columns
        borders: Border style (0=none, 1=outer, 2=all)
        alignment: Column alignment (0=left, 1=center, 2=right)
        
    Returns:
        String containing LaTeX code for the table
    """
    result = ""
    table = ""
    begin_tabular = "\\begin{tabular}{"
    end_tabular = "\n\\end{tabular}\n"
    line = "\n\\hline"
    
    if borders:
        begin_tabular += "|"
        
    for i in range(cols):
        begin_tabular += ALIGN_TYPE[alignment]
        if borders == 2 or (borders == 1 and i == cols - 1):
            begin_tabular += "|"
            
    begin_tabular += "}"
    
    if borders:
        table += line
        
    for i in range(rows):
        table += "\n\t"
        for j in range(cols):
            tmp = f"{i+1}{j+1}"
            table += tmp
            if j != cols - 1:
                table += " & "
            else:
                table += "\\\\"
                
        if borders == 2 or (borders == 1 and i == rows - 1):
            table += line
            
    result = begin_tabular + table + end_tabular
    return result

def importer_generate_matrix(bracket, rows, cols):
    """
    Generate LaTeX code for a matrix.
    
    Args:
        bracket: Matrix bracket style (0-5)
        rows: Number of rows
        cols: Number of columns
        
    Returns:
        String containing LaTeX code for the matrix
    """
    result = "$\\begin{" + BRACKET_TYPE[bracket] + "}"
    
    for i in range(rows):
        result += "\n\t"
        for j in range(cols):
            tmp = f"{i+1}{j+1}"
            result += tmp
            if j != cols - 1:
                result += " & "
            else:
                result += "\\\\"
                
    result += "\n\\end{" + BRACKET_TYPE[bracket] + "}$\n"
    return result

def importer_generate_image(filepath, caption, label, scale):
    """
    Generate LaTeX code for including an image.
    
    Args:
        filepath: Path to the image file
        caption: Image caption text
        label: Reference label for the image
        scale: Scale factor for the image
        
    Returns:
        String containing LaTeX code for the image inclusion
    """
    # Windows filepath notation correction
    if platform.system() == "Windows":
        if " " in filepath:
            # Get active editor from environment
            g_active_editor = environment.gummi_get_active_editor()
            editor.editor_insert_package(g_active_editor, "grffile", "space")
    
    # Format scale with dot as decimal separator
    scale_str = f"{scale:.2f}"
    
    # Construct the LaTeX code
    result = (f"\\begin{{figure}}[htp]\n\\centering\n"
              f"\\includegraphics[scale={scale_str}]{{{filepath}}}\n"
              f"\\caption{{{caption}}}\n\\label{{{label}}}\n"
              f"\\end{{figure}}")
    
    return result