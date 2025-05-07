#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Document class

This module defines the Document class for managing LaTeX documents,
including file operations and compilation.
"""

import os
import subprocess
import tempfile
import re
import shutil

class Document:
    """
    Represents a LaTeX document with file operations and compilation.
    
    This class manages a LaTeX document including loading, saving,
    and compiling the document to produce PDF output.
    """
    
    def __init__(self, filename=None):
        """Initialize a document, optionally from a file."""
        self.filename = filename
        self.content = ""
        self.compiled_pdf = None
        self.compile_log = ""
        self.compile_errors = []
        
        # Load content if filename is provided
        if filename and os.path.exists(filename):
            self.load()
    
    def load(self):
        """Load document content from the file."""
        if not self.filename or not os.path.exists(self.filename):
            raise FileNotFoundError(f"File not found: {self.filename}")
        
        try:
            with open(self.filename, 'r', encoding='utf-8') as f:
                self.content = f.read()
            return True
        except Exception as e:
            raise IOError(f"Error reading file: {e}")
    
    def save(self):
        """Save document content to the file."""
        if not self.filename:
            raise ValueError("No filename specified")
        
        try:
            # Create directory if it doesn't exist
            directory = os.path.dirname(self.filename)
            if directory and not os.path.exists(directory):
                os.makedirs(directory)
            
            with open(self.filename, 'w', encoding='utf-8') as f:
                f.write(self.content)
            return True
        except Exception as e:
            raise IOError(f"Error writing file: {e}")
    
    def get_content(self):
        """Get the document content."""
        return self.content
    
    def set_content(self, content):
        """Set the document content."""
        self.content = content
    
    def get_filename(self):
        """Get the document filename."""
        return self.filename
    
    def set_filename(self, filename):
        """Set the document filename."""
        self.filename = filename
    
    def compile(self, engine="pdflatex"):
        """
        Compile the LaTeX document to PDF.
        
        Args:
            engine: LaTeX compiler engine (pdflatex, xelatex, lualatex)
            
        Returns:
            Tuple (success, log_output)
        """
        if not self.filename:
            raise ValueError("Cannot compile: No filename specified")
        
        # Create a temporary directory for compilation
        temp_dir = tempfile.mkdtemp(prefix="silktex_")
        
        try:
            # Get the base name and directory of the file
            base_dir = os.path.dirname(self.filename)
            base_name = os.path.basename(self.filename)
            
            # If content has changed, save it first
            self.save()
            
            # Change to the document directory to find included files
            os.chdir(base_dir)
            
            # Run the LaTeX compiler
            cmd = [
                engine,
                "-interaction=nonstopmode",
                "-halt-on-error",
                "-file-line-error",
                "-output-directory=" + temp_dir,
                base_name
            ]
            
            # Execute the command
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            stdout, stderr = process.communicate()
            
            # Store the log output
            log_file = os.path.join(temp_dir, os.path.splitext(base_name)[0] + ".log")
            if os.path.exists(log_file):
                with open(log_file, 'r', encoding='utf-8', errors='replace') as f:
                    self.compile_log = f.read()
            else:
                self.compile_log = stdout + "\n" + stderr
            
            # Check if compilation was successful
            success = process.returncode == 0
            
            # If successful, copy the PDF to the document directory
            if success:
                pdf_file = os.path.join(temp_dir, os.path.splitext(base_name)[0] + ".pdf")
                if os.path.exists(pdf_file):
                    target_pdf = os.path.join(base_dir, os.path.splitext(base_name)[0] + ".pdf")
                    shutil.copy2(pdf_file, target_pdf)
                    self.compiled_pdf = target_pdf
            else:
                # Extract errors from the log
                self.parse_errors()
            
            return success, self.compile_log
            
        except Exception as e:
            self.compile_log = f"Compilation error: {str(e)}"
            return False, self.compile_log
        
        finally:
            # Clean up temporary directory
            try:
                shutil.rmtree(temp_dir)
            except Exception as e:
                print(f"Warning: Could not remove temporary directory: {e}")
    
    def parse_errors(self):
        """Parse LaTeX compilation errors from the log."""
        self.compile_errors = []
        
        # Look for error patterns in the log
        error_pattern = r'(?:^!|^l\.\d+)(.*?)(?=^l\.\d+|^!|$)'
        line_pattern = r'l\.(\d+)'
        
        # Extract errors
        for match in re.finditer(error_pattern, self.compile_log, re.MULTILINE | re.DOTALL):
            error_text = match.group(1).strip()
            
            # Try to extract line number
            line_match = re.search(line_pattern, match.group(0))
            line_num = int(line_match.group(1)) if line_match else -1
            
            self.compile_errors.append({
                'line': line_num,
                'message': error_text
            })
    
    def get_errors(self):
        """Get compilation errors."""
        return self.compile_errors
    
    def get_pdf_path(self):
        """Get the path to the compiled PDF."""
        return self.compiled_pdf
    
    def get_log(self):
        """Get the compilation log."""
        return self.compile_log
