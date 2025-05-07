#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - LaTeX Compilation module

This module handles the compilation of LaTeX documents to PDF
and provides error reporting and feedback.
"""

import os
import subprocess
import threading
import tempfile
import time
import re
import shutil
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - LaTeX Compilation module

This module handles LaTeX compilation for documents, error detection,
and output processing.
"""

import os
import re
import shutil
import logging
import tempfile
import subprocess
import threading
from pathlib import Path


class LatexCompiler:
    """LaTeX compiler class for compiling LaTeX documents"""
    
    def __init__(self, config=None):
        """Initialize the LaTeX compiler
        
        Args:
            config: Optional configuration dictionary
        """
        self.config = config or {}
        
        # Default configuration values
        self.latex_cmd = self.config.get('latex_cmd', 'pdflatex')
        self.bibtex_cmd = self.config.get('bibtex_cmd', 'bibtex')
        self.use_shell_escape = self.config.get('use_shell_escape', False)
        self.timeout = self.config.get('timeout', 60)  # seconds
        self.max_passes = self.config.get('max_passes', 3)
        
        # Temp directory for in-memory compilation
        self.temp_dir = None
        
        # Regular expressions for error detection
        self.error_regex = re.compile(r'(?:^! |^l\.\d+ )(.*?)$', re.MULTILINE)
        self.warning_regex = re.compile(r'LaTeX Warning:(.*?)$', re.MULTILINE)
        self.file_line_regex = re.compile(r'(\S+\.tex):(\d+): (.*?)$', re.MULTILINE)
        
        # Check for LaTeX installation
        self._check_latex_installation()
    
    def _check_latex_installation(self):
        """Check if LaTeX is installed and available"""
        try:
            result = subprocess.run(
                [self.latex_cmd, '--version'], 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE,
                text=True,
                timeout=2
            )
            if result.returncode != 0:
                logging.warning(f"LaTeX installation check failed: {self.latex_cmd} returned non-zero exit code")
        except (subprocess.SubprocessError, FileNotFoundError) as e:
            logging.warning(f"LaTeX installation check failed: {str(e)}")
    
    def compile_file(self, file_path, callback=None):
        """Compile a LaTeX file
        
        Args:
            file_path: Path to the LaTeX file
            callback: Optional callback function to call with results
            
        Returns:
            tuple: (success, pdf_path, log_text, error_message)
        """
        if not os.path.exists(file_path):
            result = (False, None, "", f"File not found: {file_path}")
            if callback:
                callback(*result)
            return result
        
        # Get the directory and filename
        directory = os.path.dirname(file_path)
        base_name = os.path.splitext(os.path.basename(file_path))[0]
        
        # Set up paths
        pdf_path = os.path.join(directory, f"{base_name}.pdf")
        log_path = os.path.join(directory, f"{base_name}.log")
        
        # Compile the document
        success, log_text, error_message = self._run_latex_compiler(
            file_path, directory, base_name
        )
        
        # Return the result
        result = (success, pdf_path if success else None, log_text, error_message)
        
        # Call the callback if provided
        if callback:
            callback(*result)
        
        return result
    
    def compile_text(self, text, callback=None):
        """Compile LaTeX text in memory
        
        Args:
            text: LaTeX document text
            callback: Optional callback function to call with results
            
        Returns:
            tuple: (success, pdf_path, log_text, error_message)
        """
        # Create a temporary directory
        self.temp_dir = tempfile.mkdtemp(prefix="silktex_")
        
        try:
            # Create a temporary file with the LaTeX content
            temp_file_path = os.path.join(self.temp_dir, "document.tex")
            with open(temp_file_path, 'w', encoding='utf-8') as f:
                f.write(text)
            
            # Compile the document
            success, log_text, error_message = self._run_latex_compiler(
                temp_file_path, self.temp_dir, "document"
            )
            
            # Path to the generated PDF
            pdf_path = os.path.join(self.temp_dir, "document.pdf")
            
            # Return the result
            result = (success, pdf_path if success else None, log_text, error_message)
            
            # Call the callback if provided
            if callback:
                callback(*result)
            
            return result
            
        except Exception as e:
            logging.error(f"Error compiling LaTeX text: {str(e)}")
            result = (False, None, "", f"Compilation error: {str(e)}")
            
            # Call the callback if provided
            if callback:
                callback(*result)
            
            return result
    
    def _run_latex_compiler(self, tex_file, work_dir, base_name):
        """Run the LaTeX compiler on a file
        
        Args:
            tex_file: Path to the LaTeX file
            work_dir: Working directory for compilation
            base_name: Base name of the LaTeX file (without extension)
            
        Returns:
            tuple: (success, log_text, error_message)
        """
        # Determine if the document has a bibliography
        has_bibliography = self._check_for_bibliography(tex_file)
        
        # Build the LaTeX command
        latex_cmd = [self.latex_cmd]
        
        # Add options
        latex_cmd.extend([
            "-interaction=nonstopmode",
            "-file-line-error",
            "-halt-on-error"
        ])
        
        # Add shell escape if configured
        if self.use_shell_escape:
            latex_cmd.append("-shell-escape")
        
        # Add output directory if needed
        if work_dir != os.path.dirname(tex_file):
            latex_cmd.extend(["-output-directory", work_dir])
        
        # Add the input file
        latex_cmd.append(tex_file)
        
        # Run the initial compilation
        logging.info(f"Running LaTeX: {' '.join(latex_cmd)}")
        
        try:
            # First pass
            process = subprocess.run(
                latex_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=work_dir,
                timeout=self.timeout
            )
            
            # Get the log text
            log_file = os.path.join(work_dir, f"{base_name}.log")
            log_text = self._read_log_file(log_file)
            
            # Check for errors in the first pass
            if process.returncode != 0:
                error_message = self._extract_error_message(log_text)
                return False, log_text, error_message
            
            # Run BibTeX if needed
            if has_bibliography:
                bibtex_result = self._run_bibtex(work_dir, base_name)
                if not bibtex_result[0]:
                    return bibtex_result
            
            # Run additional LaTeX passes if needed
            for _ in range(1, self.max_passes):
                # Check if another pass is needed
                if not self._needs_another_pass(log_text):
                    break
                
                # Run another pass
                process = subprocess.run(
                    latex_cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    cwd=work_dir,
                    timeout=self.timeout
                )
                
                # Update log text
                log_text = self._read_log_file(log_file)
                
                # Check for errors
                if process.returncode != 0:
                    error_message = self._extract_error_message(log_text)
                    return False, log_text, error_message
            
            # Final pass successful
            return True, log_text, ""
            
        except subprocess.TimeoutExpired:
            return False, "", "LaTeX compilation timed out"
        except Exception as e:
            logging.error(f"LaTeX compilation error: {str(e)}")
            return False, "", f"LaTeX compilation error: {str(e)}"
    
    def _run_bibtex(self, work_dir, base_name):
        """Run BibTeX on the compiled document
        
        Args:
            work_dir: Working directory containing the .aux file
            base_name: Base name of the LaTeX file (without extension)
            
        Returns:
            tuple: (success, log_text, error_message)
        """
        # Check if the .aux file exists
        aux_file = os.path.join(work_dir, f"{base_name}.aux")
        if not os.path.exists(aux_file):
            return True, "", ""  # Not an error, just skip BibTeX
        
        # Build the BibTeX command
        bibtex_cmd = [self.bibtex_cmd, base_name]
        
        try:
            # Run BibTeX
            logging.info(f"Running BibTeX: {' '.join(bibtex_cmd)}")
            process = subprocess.run(
                bibtex_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=work_dir,
                timeout=self.timeout
            )
            
            # Get the BibTeX log
            blg_file = os.path.join(work_dir, f"{base_name}.blg")
            log_text = self._read_log_file(blg_file) if os.path.exists(blg_file) else process.stdout
            
            # Check for errors
            if process.returncode != 0:
                error_message = "BibTeX processing failed"
                return False, log_text, error_message
            
            return True, log_text, ""
            
        except subprocess.TimeoutExpired:
            return False, "", "BibTeX processing timed out"
        except Exception as e:
            logging.error(f"BibTeX error: {str(e)}")
            return False, "", f"BibTeX error: {str(e)}"
    
    def _check_for_bibliography(self, tex_file):
        """Check if the document contains a bibliography
        
        Args:
            tex_file: Path to the LaTeX file
            
        Returns:
            bool: True if the document has a bibliography, False otherwise
        """
        try:
            with open(tex_file, 'r', encoding='utf-8') as f:
                content = f.read()
                
            # Check for bibliography commands
            bib_patterns = [
                r'\\bibliography\{',
                r'\\addbibresource\{',
                r'\\begin\{thebibliography\}',
                r'\\printbibliography'
            ]
            
            for pattern in bib_patterns:
                if re.search(pattern, content):
                    return True
            
            return False
            
        except Exception as e:
            logging.error(f"Error checking for bibliography: {str(e)}")
            return False
    
    def _needs_another_pass(self, log_text):
        """Check if another LaTeX pass is needed
        
        Args:
            log_text: The LaTeX log text
            
        Returns:
            bool: True if another pass is needed, False otherwise
        """
        rerun_patterns = [
            r'Rerun to get cross-references right',
            r'Rerun to get bibliographical references right',
            r'Rerun to get citations correct',
            r'Rerun LaTeX',
            r'Please rerun LaTeX',
            r'Label\(s\) may have changed',
            r'There were undefined references'
        ]
        
        for pattern in rerun_patterns:
            if re.search(pattern, log_text):
                return True
        
        return False
    
    def _read_log_file(self, log_file):
        """Read a log file
        
        Args:
            log_file: Path to the log file
            
        Returns:
            str: The log file contents or an empty string if not found
        """
        try:
            if os.path.exists(log_file):
                with open(log_file, 'r', encoding='utf-8', errors='replace') as f:
                    return f.read()
            return ""
        except Exception as e:
            logging.error(f"Error reading log file: {str(e)}")
            return ""
    
    def _extract_error_message(self, log_text):
        """Extract error message from LaTeX log
        
        Args:
            log_text: The LaTeX log text
            
        Returns:
            str: The extracted error message
        """
        # Look for error messages
        error_match = self.error_regex.search(log_text)
        if error_match:
            return error_match.group(1).strip()
        
        # Look for file and line errors
        file_line_match = self.file_line_regex.search(log_text)
        if file_line_match:
            file_name = file_line_match.group(1)
            line_num = file_line_match.group(2)
            error_msg = file_line_match.group(3)
            return f"{file_name} (line {line_num}): {error_msg}"
        
        # Generic error message
        return "LaTeX compilation failed. Check log for details."
    
    def get_errors_and_warnings(self, log_text):
        """Extract errors and warnings from LaTeX log
        
        Args:
            log_text: The LaTeX log text
            
        Returns:
            tuple: (errors, warnings) where each is a list of dictionaries
        """
        errors = []
        warnings = []
        
        # Extract errors
        for match in self.error_regex.finditer(log_text):
            error_text = match.group(1).strip()
            errors.append({
                'message': error_text,
                'line': None,
                'file': None
            })
        
        # Extract file and line errors
        for match in self.file_line_regex.finditer(log_text):
            file_name = match.group(1)
            line_num = int(match.group(2))
            error_msg = match.group(3)
            errors.append({
                'message': error_msg,
                'line': line_num,
                'file': file_name
            })
        
        # Extract warnings
        for match in self.warning_regex.finditer(log_text):
            warning_text = match.group(1).strip()
            warnings.append({
                'message': warning_text,
                'line': None,
                'file': None
            })
        
        return errors, warnings
    
    def cleanup(self):
        """Clean up temporary files"""
        if self.temp_dir and os.path.exists(self.temp_dir):
            try:
                shutil.rmtree(self.temp_dir)
                self.temp_dir = None
            except Exception as e:
                logging.error(f"Error cleaning up temporary files: {str(e)}")


class LatexDocument:
    """Class representing a LaTeX document with compilation features"""
    
    def __init__(self, file_path=None, text=None):
        """Initialize a LaTeX document
        
        Args:
            file_path: Optional path to a LaTeX file
            text: Optional LaTeX document text
        """
        self.file_path = file_path
        self.text = text
        self.compiler = LatexCompiler()
        self.pdf_path = None
        self.log_text = ""
        self.errors = []
        self.warnings = []
    
    def load_file(self, file_path):
        """Load a LaTeX file
        
        Args:
            file_path: Path to the LaTeX file
            
        Returns:
            bool: True if successful, False otherwise
        """
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                self.text = f.read()
            
            self.file_path = file_path
            return True
        except Exception as e:
            logging.error(f"Error loading LaTeX file: {str(e)}")
            return False
    
    def save_file(self, file_path=None):
        """Save the LaTeX document to a file
        
        Args:
            file_path: Optional path to save to (uses existing path if None)
            
        Returns:
            bool: True if successful, False otherwise
        """
        if not self.text:
            return False
        
        path = file_path or self.file_path
        if not path:
            return False
        
        try:
            with open(path, 'w', encoding='utf-8') as f:
                f.write(self.text)
            
            if file_path:
                self.file_path = file_path
            
            return True
        except Exception as e:
            logging.error(f"Error saving LaTeX file: {str(e)}")
            return False
    
    def compile(self, callback=None):
        """Compile the LaTeX document
        
        Args:
            callback: Optional callback function to call with results
            
        Returns:
            bool: True if compilation was successful, False otherwise
        """
        if self.file_path and os.path.exists(self.file_path):
            # Compile from file
            success, pdf_path, log_text, error_message = self.compiler.compile_file(
                self.file_path,
                callback=lambda s, p, l, e: self._handle_compilation_result(s, p, l, e, callback)
            )
        elif self.text:
            # Compile from text
            success, pdf_path, log_text, error_message = self.compiler.compile_text(
                self.text,
                callback=lambda s, p, l, e: self._handle_compilation_result(s, p, l, e, callback)
            )
        else:
            success, pdf_path, log_text, error_message = False, None, "", "No document to compile"
            if callback:
                callback(success, pdf_path, log_text, error_message)
        
        return success
    
    def _handle_compilation_result(self, success, pdf_path, log_text, error_message, user_callback=None):
        """Handle compilation results and extract errors and warnings
        
        Args:
            success: Whether compilation was successful
            pdf_path: Path to the output PDF file
            log_text: Compilation log text
            error_message: Error message if compilation failed
            user_callback: User callback to call after processing
        """
        # Store results
        self.pdf_path = pdf_path
        self.log_text = log_text
        
        # Extract errors and warnings
        if log_text:
            self.errors, self.warnings = self.compiler.get_errors_and_warnings(log_text)
        
        # Call user callback if provided
        if user_callback:
            user_callback(success, pdf_path, log_text, error_message)


def is_latex_installed():
    """Check if LaTeX is installed on the system
    
    Returns:
        bool: True if LaTeX is installed, False otherwise
    """
    try:
        result = subprocess.run(
            ['pdflatex', '--version'], 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE,
            text=True,
            timeout=2
        )
        return result.returncode == 0
    except (subprocess.SubprocessError, FileNotFoundError):
        return False
from gi.repository import GLib


class LaTeXCompiler:
    """LaTeX compilation handler"""
    
    def __init__(self, window):
        """Initialize the LaTeX compiler
        
        Args:
            window: Parent window for status updates
        """
        self.window = window
        self.temp_dir = tempfile.mkdtemp(prefix="silktex_")
        self.engine = "pdflatex"
        self.last_pdf = None
        self.error_regex = re.compile(r'(?:.*:(\d+):|l\.(\d+)).*(?:error|undefined|not found|missing|extra|forbidden)', re.IGNORECASE)
        self.warning_regex = re.compile(r'LaTeX Warning: .* on (input )?line (\d+)\.', re.IGNORECASE)
        self.badbox_regex = re.compile(r'(Overfull|Underfull) \\[hv]box .* at lines? (\d+)', re.IGNORECASE)
    
    def set_engine(self, engine):
        """Set the LaTeX engine to use
        
        Args:
            engine: LaTeX engine name (pdflatex, xelatex, lualatex)
        """
        self.engine = engine
    
    def compile_document(self, editor=None):
        """Compile the current document
        
        Args:
            editor: The LaTeX editor component (optional)
        
        Returns:
            bool: True if compilation was started, False otherwise
        """
        if not self.window.current_file:
            if not editor or not editor.get_text().strip():
                self.window.update_status("No document to compile")
                return False
                
            # Save to a temporary file
            tmp_tex = os.path.join(self.temp_dir, "document.tex")
            with open(tmp_tex, "w", encoding="utf-8") as f:
                f.write(editor.get_text())
            tex_path = tmp_tex
        else:
            tex_path = self.window.current_file.get_path()
        
        if not tex_path:
            self.window.update_status("No file to compile")
            return False
        
        # Run in a separate thread to avoid blocking the UI
        thread = threading.Thread(target=self._run_compilation, args=(tex_path,))
        thread.daemon = True
        thread.start()
        
        return True
    
    def _run_compilation(self, tex_path):
        """Run LaTeX compilation in a separate thread
        
        Args:
            tex_path: Path to the LaTeX file
        """
        GLib.idle_add(self.window.update_status, f"Compiling with {self.engine}...")
        
        # Get the directory of the TeX file
        work_dir = os.path.dirname(tex_path)
        if not work_dir:
            work_dir = "."
        
        file_name = os.path.basename(tex_path)
        file_base = os.path.splitext(file_name)[0]
        
        # Run LaTeX command
        cmd = [
            self.engine,
            "-interaction=nonstopmode",
            "-file-line-error",
            file_name
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=work_dir,
                capture_output=True,
                text=True,
                timeout=30
            )
            
            # Check if we need to run BibTeX
            if "No file {}.bbl".format(file_base) in result.stdout or "Citation" in result.stdout:
                # Run BibTeX
                GLib.idle_add(self.window.update_status, "Running BibTeX...")
                bibtex_cmd = ["bibtex", file_base]
                bibtex_result = subprocess.run(
                    bibtex_cmd,
                    cwd=work_dir,
                    capture_output=True,
                    text=True,
                    timeout=10
                )
                
                # Run LaTeX again (twice) after BibTeX to resolve references
                GLib.idle_add(self.window.update_status, f"Running {self.engine} pass 2...")
                subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True, timeout=30)
                
                GLib.idle_add(self.window.update_status, f"Running {self.engine} final pass...")
                result = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True, timeout=30)
            
            if result.returncode != 0:
                # Compilation failed
                error_msg, error_line = self._extract_error(result.stdout)
                
                # Report error to window
                GLib.idle_add(self.window.update_status, f"Compilation failed: {error_msg}")
                
                # Highlight error line if we found one and have an editor
                if error_line and hasattr(self.window, "editor"):
                    GLib.idle_add(self._highlight_error_line, error_line)
                
                return False
            
            # Check for warnings
            warning_msg, warning_line = self._extract_warning(result.stdout)
            if warning_msg:
                GLib.idle_add(self.window.update_status, f"Compilation successful with warnings: {warning_msg}")
            
            # Compilation succeeded
            pdf_path = os.path.join(work_dir, file_base + ".pdf")
            if os.path.exists(pdf_path):
                self.last_pdf = pdf_path
                GLib.idle_add(self._update_preview, pdf_path)
                if not warning_msg:
                    GLib.idle_add(self.window.update_status, "Compilation successful")
                return True
            else:
                GLib.idle_add(self.window.update_status, "Compilation failed: PDF not generated")
                return False
                
        except subprocess.TimeoutExpired:
            GLib.idle_add(self.window.update_status, "Compilation timed out")
            return False
        except Exception as e:
            GLib.idle_add(self.window.update_status, f"Compilation error: {str(e)}")
            return False
    
    def _extract_error(self, output):
        """Extract error message and line number from LaTeX output
        
        Args:
            output: LaTeX command output
            
        Returns:
            tuple: (error_message, line_number or None)
        """
        lines = output.splitlines()
        
        # Look for error lines
        for i, line in enumerate(lines):
            match = self.error_regex.search(line)
            if match:
                # Found an error line
                line_num = match.group(1) or match.group(2)
                error_msg = line.strip()
                
                # Look for more context in the next few lines
                context_count = 0
                j = i + 1
                while j < len(lines) and context_count < 3:
                    if lines[j].strip() and not lines[j].startswith(" "):
                        error_msg += " " + lines[j].strip()
                        context_count += 1
                    j += 1
                
                # Truncate if too long
                if len(error_msg) > 100:
                    error_msg = error_msg[:97] + "..."
                
                return error_msg, int(line_num) if line_num else None
        
        # No specific error found
        return "Unknown error", None
    
    def _extract_warning(self, output):
        """Extract warning message and line number from LaTeX output
        
        Args:
            output: LaTeX command output
            
        Returns:
            tuple: (warning_message, line_number or None)
        """
        lines = output.splitlines()
        
        # Look for warning lines
        for line in lines:
            # Check for LaTeX warnings
            match = self.warning_regex.search(line)
            if match:
                line_num = match.group(2)
                return line.strip(), int(line_num) if line_num else None
            
            # Check for bad boxes
            match = self.badbox_regex.search(line)
            if match:
                line_num = match.group(2)
                return line.strip(), int(line_num) if line_num else None
        
        # No warning found
        return None, None
    
    def _highlight_error_line(self, line_number):
        """Highlight the error line in the editor
        
        Args:
            line_number: Line number to highlight
        """
        # Get buffer and create tag for error highlighting
        buffer = self.window.editor.get_buffer()
        
        # Check if we already have an error tag
        error_tag = buffer.get_tag_table().lookup("latex-error")
        if not error_tag:
            error_tag = buffer.create_tag("latex-error", 
                                          background="rgba(255,0,0,0.3)",
                                          underline=True,
                                          underline_rgba="red")
        
        # Remove any existing error highlights
        start = buffer.get_start_iter()
        end = buffer.get_end_iter()
        buffer.remove_tag(error_tag, start, end)
        
        # Apply tag to the error line
        start = buffer.get_iter_at_line(line_number - 1)  # Convert to 0-based
        end = buffer.get_iter_at_line(line_number)
        buffer.apply_tag(error_tag, start, end)
        
        # Scroll to the error line
        mark = buffer.create_mark("error_line", start, False)
        self.window.editor.scroll_to_mark(mark, 0.25, False, 0.5, 0.5)
    
    def _update_preview(self, pdf_path):
        """Update the PDF preview with the compiled document
        
        Args:
            pdf_path: Path to the compiled PDF
        """
        if hasattr(self.window, "preview"):
            self.window.preview.load_pdf(pdf_path)
    
    def clean_auxiliary_files(self, tex_path):
        """Clean up auxiliary files generated by LaTeX
        
        Args:
            tex_path: Path to the LaTeX file
        """
        if not tex_path:
            return
            
        base_name = os.path.splitext(tex_path)[0]
        dir_path = os.path.dirname(tex_path)
        
        # Extensions of auxiliary files to clean
        aux_extensions = [
            ".aux", ".log", ".toc", ".lof", ".lot", ".bbl", 
            ".blg", ".out", ".nav", ".snm", ".synctex.gz", 
            ".fls", ".fdb_latexmk", ".dvi"
        ]
        
        for ext in aux_extensions:
            aux_file = base_name + ext
            if os.path.exists(aux_file):
                try:
                    os.remove(aux_file)
                except OSError:
                    pass
    
    def cleanup_temp_files(self):
        """Clean up temporary compilation files"""
        try:
            if os.path.exists(self.temp_dir):
                shutil.rmtree(self.temp_dir)
        except OSError as e:
            print(f"Error cleaning temporary files: {e}")
