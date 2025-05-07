#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - LaTeX compilation module

This module handles the compilation of LaTeX documents to PDF
for both preview and export purposes.
"""

import os
import subprocess
import tempfile
import threading
import re
import gi

gi.require_version('Gtk', '4.0')
from gi.repository import Gtk, GLib

class LaTeXCompiler:
    """
    LaTeX compilation handler.
    
    This class handles compiling LaTeX documents to PDF format,
    managing errors, and providing feedback to the user.
    """
    
    def __init__(self, window):
        """Initialize the LaTeX compiler."""
        self.window = window
        self.compile_thread = None
        self.last_compiled_text = ""
        self.compile_in_progress = False
        self.temp_dir = tempfile.TemporaryDirectory(prefix="silktex_")
        self.log_errors = []
    
    def compile_for_preview(self):
        """Compile the current document for preview."""
        # Don't run multiple compilations at once
        if self.compile_in_progress:
            return
        
        # Get the current document text
        current_text = self.window.editor.get_text()
        
        # Skip if no changes since last compilation
        if current_text == self.last_compiled_text:
            return
        
        # Compile in a separate thread to keep UI responsive
        self.compile_in_progress = True
        self.window.update_status("Compiling...")
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(
            target=self._run_compilation,
            args=(current_text, False)
        )
        self.compile_thread.daemon = True
        self.compile_thread.start()
    
    def compile_and_save_pdf(self):
        """Compile and save the PDF to a user-chosen location."""
        # Check if file is saved first
        if self.window.editor.get_file_path() is None:
            dialog = Gtk.MessageDialog(
                transient_for=self.window,
                message_type=Gtk.MessageType.INFO,
                buttons=Gtk.ButtonsType.OK,
                text="Please save your document first."
            )
            dialog.format_secondary_text(
                "The document needs to be saved before exporting to PDF."
            )
            dialog.connect("response", lambda d, r: d.destroy())
            dialog.present()
            return
        
        # Get the output file path
        dialog = Gtk.FileChooserDialog(
            title="Export to PDF",
            parent=self.window,
            action=Gtk.FileChooserAction.SAVE
        )
        dialog.add_button("Cancel", Gtk.ResponseType.CANCEL)
        dialog.add_button("Export", Gtk.ResponseType.ACCEPT)
        dialog.set_do_overwrite_confirmation(True)
        
        # Set default file name based on the input file
        input_file = self.window.editor.get_file_path()
        default_name = os.path.basename(input_file)
        if default_name.endswith('.tex'):
            default_name = default_name[:-4] + '.pdf'
        else:
            default_name += '.pdf'
        
        dialog.set_current_name(default_name)
        
        # Add file filter for PDF files
        pdf_filter = Gtk.FileFilter()
        pdf_filter.set_name("PDF files")
        pdf_filter.add_pattern("*.pdf")
        dialog.add_filter(pdf_filter)
        
        # Add filter for all files
        all_filter = Gtk.FileFilter()
        all_filter.set_name("All files")
        all_filter.add_pattern("*")
        dialog.add_filter(all_filter)
        
        dialog.connect("response", self._on_export_dialog_response)
        dialog.present()
    
    def _on_export_dialog_response(self, dialog, response):
        """Handle response from the export dialog."""
        if response == Gtk.ResponseType.ACCEPT:
            output_file = dialog.get_file().get_path()
            
            # Add .pdf extension if none provided
            if not output_file.endswith('.pdf'):
                output_file += '.pdf'
            
            # Get the current document text
            current_text = self.window.editor.get_text()
            
            # Compile and save the PDF
            self.compile_in_progress = True
            self.window.update_status("Exporting to PDF...")
            
            # Start compilation in a separate thread
            self.compile_thread = threading.Thread(
                target=self._run_compilation,
                args=(current_text, True, output_file)
            )
            self.compile_thread.daemon = True
            self.compile_thread.start()
        
        dialog.destroy()
    
    def _run_compilation(self, text, export=False, output_file=None):
        """Run the LaTeX compilation process.
        
        Args:
            text: The LaTeX document text to compile
            export: Whether this is an export operation
            output_file: The output PDF file path for export
        """
        try:
            # Create a temporary file for compilation
            temp_dir = self.temp_dir.name
            tex_file = os.path.join(temp_dir, "temp.tex")
            
            # Write the document to the temporary file
            with open(tex_file, 'w', encoding='utf-8') as f:
                f.write(text)
            
            # Set up the compilation command
            latex_cmd = 'pdflatex'
            
            # Check if xelatex is available and the document uses it
            if 'xelatex' in text or '\\usepackage{fontspec}' in text:
                latex_cmd = 'xelatex'
            
            # Run LaTeX compilation
            args = [
                latex_cmd,
                '-interaction=nonstopmode',
                '-halt-on-error',
                '-file-line-error',
                '-output-directory=' + temp_dir,
                tex_file
            ]
            
            # Run first pass
            process = subprocess.Popen(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            stdout, stderr = process.communicate()
            
            # Check for errors
            log_file = os.path.join(temp_dir, "temp.log")
            self.log_errors = self._parse_latex_errors(log_file)
            
            # If there are bibliography commands, run BibTeX
            if '\\bibliography{' in text or '\\addbibresource{' in text:
                bibtex_args = ['bibtex', os.path.join(temp_dir, "temp")]
                process = subprocess.Popen(
                    bibtex_args,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True
                )
                process.communicate()
                
                # Run LaTeX again after BibTeX
                process = subprocess.Popen(
                    args,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True
                )
                process.communicate()
            
            # Run second pass to resolve references
            process = subprocess.Popen(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            process.communicate()
            
            # Get the generated PDF file
            pdf_file = os.path.join(temp_dir, "temp.pdf")
            
            # Copy to the output location if exporting
            if export and output_file and os.path.exists(pdf_file):
                import shutil
                shutil.copy2(pdf_file, output_file)
                
                # Update UI from the main thread
                GLib.idle_add(self._update_ui_after_export, output_file)
            elif os.path.exists(pdf_file):
                # Load the PDF into the preview if not exporting
                GLib.idle_add(self._update_preview, pdf_file)
            else:
                # Update UI to show compilation error
                GLib.idle_add(self._show_compilation_error)
            
            # Save the compiled text to avoid unnecessary recompilation
            self.last_compiled_text = text
            
        except Exception as e:
            GLib.idle_add(self._show_error, str(e))
        
        finally:
            self.compile_in_progress = False
    
    def _update_preview(self, pdf_file):
        """Update the preview with the compiled PDF.
        
        Args:
            pdf_file: The path to the compiled PDF file
        """
        self.window.update_status("Compilation successful")
        self.window.preview.load_pdf(pdf_file)
        
        # Highlight error lines if any
        if self.log_errors:
            # TODO: Add error highlighting in the editor
            error_count = len(self.log_errors)
            self.window.update_status(f"Compilation finished with {error_count} errors")
    
    def _update_ui_after_export(self, output_file):
        """Update UI after exporting PDF.
        
        Args:
            output_file: The path to the exported PDF file
        """
        self.window.update_status(f"Exported to {os.path.basename(output_file)}")
        
        # Show success dialog
        dialog = Gtk.MessageDialog(
            transient_for=self.window,
            message_type=Gtk.MessageType.INFO,
            buttons=Gtk.ButtonsType.OK,
            text="PDF Export Complete"
        )
        dialog.format_secondary_text(f"The PDF has been exported to {output_file}")
        dialog.connect("response", lambda d, r: d.destroy())
        dialog.present()
    
    def _show_compilation_error(self):
        """Show compilation error in the UI."""
        self.window.update_status("Compilation failed. See log for details.")
        
        # Show error dialog
        dialog = Gtk.MessageDialog(
            transient_for=self.window,
            message_type=Gtk.MessageType.ERROR,
            buttons=Gtk.ButtonsType.OK,
            text="LaTeX Compilation Error"
        )
        
        if self.log_errors:
            error_text = "\n".join([f"Line {err[0]}: {err[1]}" for err in self.log_errors[:5]])
            if len(self.log_errors) > 5:
                error_text += f"\n\n(Plus {len(self.log_errors) - 5} more errors.)"
            dialog.format_secondary_text(error_text)
        else:
            dialog.format_secondary_text("Unknown error during compilation. Check your LaTeX syntax.")
        
        dialog.connect("response", lambda d, r: d.destroy())
        dialog.present()
    
    def _show_error(self, error_message):
        """Show a general error message in the UI.
        
        Args:
            error_message: The error message to display
        """
        self.window.update_status("Error during compilation")
        
        # Show error dialog
        dialog = Gtk.MessageDialog(
            transient_for=self.window,
            message_type=Gtk.MessageType.ERROR,
            buttons=Gtk.ButtonsType.OK,
            text="Error"
        )
        dialog.format_secondary_text(error_message)
        dialog.connect("response", lambda d, r: d.destroy())
        dialog.present()
    
    def _parse_latex_errors(self, log_file):
        """Parse LaTeX log file for errors.
        
        Args:
            log_file: The path to the LaTeX log file
            
        Returns:
            A list of tuples (line_number, error_message)
        """
        errors = []
        if not os.path.exists(log_file):
            return errors
        
        try:
            with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
                log_content = f.read()
            
            # Extract errors from the log
            # Pattern matches lines like: ./temp.tex:10: LaTeX Error: ...
            pattern = r'.*:(\d+):\s+(.*)'
            for match in re.finditer(pattern, log_content):
                line_num = int(match.group(1))
                error_msg = match.group(2).strip()
                errors.append((line_num, error_msg))
            
            return errors
        
        except Exception as e:
            print(f"Error parsing LaTeX log: {e}")
            return []
"""
@file   latex.py
@brief  LaTeX compilation handler for Gummi
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - LaTeX Compiler module

This module defines the LaTeX compiler for SilkTex.
"""

import os
import subprocess
import tempfile
import threading
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, GLib, Adw

class LaTeXCompiler:
    """
    LaTeX compiler for SilkTex.
    
    This class manages the compilation of LaTeX documents to PDF.
    """
    
    def __init__(self, window):
        """Initialize the LaTeX compiler.
        
        Args:
            window: The main application window
        """
        self.window = window
        self.current_engine = "pdflatex"
        self.is_compiling = False
    
    def compile_document(self, document, engine=None):
        """Compile a LaTeX document.
        
        Args:
            document: The Document object to compile
            engine: The LaTeX engine to use (pdflatex, xelatex, lualatex)
            
        Returns:
            bool: True if compilation was successful
        """
        if not document or not document.get_filename():
            return False
            
        # Use specified engine or default
        engine = engine or self.current_engine
        
        # Start compilation in a separate thread
        self.is_compiling = True
        self.window.update_status(f"Compiling with {engine}...")
        
        # Run compilation in a separate thread to avoid blocking the UI
        compile_thread = threading.Thread(
            target=self._compile_thread,
            args=(document, engine)
        )
        compile_thread.daemon = True
        compile_thread.start()
        
        return True
    
    def _compile_thread(self, document, engine):
        """Run compilation in a separate thread.
        
        Args:
            document: The Document object to compile
            engine: The LaTeX engine to use
        """
        try:
            success, log = document.compile(engine)
            
            # Update UI in the main thread
            GLib.idle_add(self._compilation_finished, document, success, log)
            
        except Exception as e:
            # Handle exceptions
            GLib.idle_add(
                self._compilation_error,
                document,
                f"Compilation error: {str(e)}"
            )
    
    def _compilation_finished(self, document, success, log):
        """Handle completion of compilation.
        
        Args:
            document: The Document object
            success: Whether compilation was successful
            log: Compilation log output
        """
        self.is_compiling = False
        
        if success:
            self.window.update_status("Compilation successful")
            
            # Show success toast
            toast = Adw.Toast.new("Document compiled successfully")
            self.window.toast_overlay.add_toast(toast)
            
            # Update preview
            if hasattr(self.window, "preview"):
                self.window.preview.set_document(document)
                
        else:
            self.window.update_status("Compilation failed")
            
            # Show error toast with action to view log
            toast = Adw.Toast.new("Compilation failed. See error log for details.")
            toast.set_button_label("View Log")
            toast.connect("button-clicked", self._show_error_log, document)
            self.window.toast_overlay.add_toast(toast)
        
        return False  # Required for GLib.idle_add
    
    def _compilation_error(self, document, error_message):
        """Handle compilation error.
        
        Args:
            document: The Document object
            error_message: Error message to display
        """
        self.is_compiling = False
        self.window.update_status("Compilation error")
        
        # Show error toast
        toast = Adw.Toast.new(error_message)
        self.window.toast_overlay.add_toast(toast)
        
        return False  # Required for GLib.idle_add
    
    def _show_error_log(self, toast, document):
        """Show the error log.
        
        Args:
            toast: The toast that triggered this action
            document: The Document object
        """
        if hasattr(self.window, "preview"):
            self.window.preview.show_error_log()
    
    def set_engine(self, engine):
        """Set the LaTeX compiler engine.
        
        Args:
            engine: The LaTeX engine to use (pdflatex, xelatex, lualatex)
        """
        self.current_engine = engine
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