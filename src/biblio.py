"""
@file   biblio.py
@brief  Bibliography management for Gummi

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
import subprocess
import sys
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource

from constants import TEXSEC
from latex import latex_update_workfile, latex_update_auxfile
from utils import utils_popen_r
from logger import slog, L_INFO, L_WARNING
from editor import GuEditor

class GuBiblio:
    """Bibliography manager for Gummi"""
    
    def __init__(self, builder):
        """Initialize bibliography manager
        
        Args:
            builder: Gtk.Builder object with UI definitions
        
        Returns:
            GuBiblio instance
        """
        if not isinstance(builder, Gtk.Builder):
            raise TypeError("builder must be a Gtk.Builder instance")
            
        self.progressbar = builder.get_object("bibprogressbar")
        self.progressval = 0.0
        
        self.list_biblios = builder.get_object("list_biblios")
        self.filenm_label = builder.get_object("bibfilenm")
        self.refnr_label = builder.get_object("bibrefnr")
        self.list_filter = builder.get_object("biblio_filter")
        self.biblio_treeview = builder.get_object("bibtreeview")

def biblio_detect_bibliography(ec):
    """Detect bibliography in LaTeX document
    
    Args:
        ec: GuEditor instance
    
    Returns:
        bool: True if bibliography was detected, False otherwise
    """
    content = None
    bibfn = None
    state = False
    
    content = ec.grab_buffer()
    bib_regex = re.compile(r'^[^%]*\\bibliography{\s*([^{}\s]*)\s*}', re.MULTILINE)
    match = bib_regex.search(content)
    
    if match:
        bibfile = match.group(1)
        if not bibfile.endswith(".bib"):
            bibfn = f"{bibfile}.bib"
        else:
            bibfn = bibfile
            
        state = ec.fileinfo_update_biblio(bibfn)
        slog(L_INFO, f"Detect bibliography file: {ec.bibfile}")
        
    return state

def biblio_compile_bibliography(bc, ec):
    """Compile bibliography using bibtex
    
    Args:
        bc: GuBiblio instance
        ec: GuEditor instance
        
    Returns:
        bool: True if compilation was successful, False otherwise
    """
    dirname = os.path.dirname(ec.workfile)
    
    if ec.filename:
        auxname = ec.pdffile[:-4]  # Remove .pdf extension
    else:
        auxname = ec.fdname
    
    bibtex_path = shutil.which("bibtex")
    if bibtex_path:
        success = False
        command = f"{TEXSEC} bibtex \"{auxname}\""
        
        latex_update_workfile(ec)
        latex_update_auxfile(ec)
        
        result = utils_popen_r(command, dirname)
        bc.progressbar.set_tooltip_text(result[1])  # Assuming result is a tuple where second element is stdout/stderr
        
        success = "Database file #1" in result[1]
        return success
    
    slog(L_WARNING, "bibtex command is not present or executable.")
    return False

def biblio_parse_entries(bc, bib_content):
    """Parse BibTeX entries from bibliography file
    
    Args:
        bc: GuBiblio instance
        bib_content: String content of bibliography file
        
    Returns:
        int: Number of entries parsed
    """
    entry_total = 0
    
    # Regular expressions for parsing bibliography entries
    regex_entry = re.compile(
        r'(@article|@book|@booklet|@conference|@inbook|@incollection|'
        r'@inproceedings|@manual|@mastersthesis|@misc|@phdthesis|'
        r'@proceedings|@techreport|@unpublished)([^@]*)',
        re.IGNORECASE | re.DOTALL)
    
    subregex_ident = re.compile(r'@.+{([^,]+),')
    subregex_title = re.compile(r'[^book]title\s*=\s*(.*)', re.IGNORECASE)
    subregex_author = re.compile(r'author\s*=\s*(.*)', re.IGNORECASE)
    subregex_year = re.compile(r'year\s*=\s*[{|"]?([1|2][0-9]{3})', re.IGNORECASE)
    regex_formatting = re.compile(r'[{|}|"|,|\$]')
    
    for match in regex_entry.finditer(bib_content):
        entry = match.group(0)
        
        # Extract identifier
        ident_match = subregex_ident.search(entry)
        ident = ident_match.group(1) if ident_match else None
        
        # Extract title
        title_match = subregex_title.search(entry)
        if title_match:
            title_out = regex_formatting.sub('', title_match.group(1))
        else:
            title_out = None
        
        # Extract author
        author_match = subregex_author.search(entry)
        if author_match:
            author_out = regex_formatting.sub('', author_match.group(1))
        else:
            author_out = None
        
        # Extract year
        year_match = subregex_year.search(entry)
        year = year_match.group(1) if year_match else None
        
        # Add to list store
        iter_val = bc.list_biblios.append()
        bc.list_biblios.set(iter_val, 
                            0, ident,
                            1, title_out,
                            2, author_out,
                            3, year)
        
        entry_total += 1
    
    return entry_total