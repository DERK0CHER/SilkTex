# document.py - Document handling utilities
import os
import shutil
import subprocess
import tempfile


class Document:
    """Class representing a LaTeX document"""
    
    def __init__(self, filename=None):
        """Initialize document with optional filename"""
        self.filename = filename
        self.content = ""
        
        # Load content if a filename is provided
        if filename and os.path.exists(filename):
            self.load()
    
    def load(self):
        """Load document content from file"""
        if not self.filename:
            raise ValueError("No filename specified")
        
        try:
            with open(self.filename, 'r', encoding='utf-8') as f:
                self.content = f.read()
        except Exception as e:
            raise IOError(f"Error loading document: {str(e)}")
    
    def save(self):
        """Save document content to file"""
        if not self.filename:
            raise ValueError("No filename specified")
        
        try:
            # Create directory if it doesn't exist
            directory = os.path.dirname(self.filename)
            if directory and not os.path.exists(directory):
                os.makedirs(directory)
            
            with open(self.filename, 'w', encoding='utf-8') as f:
                f.write(self.content)
        except Exception as e:
            raise IOError(f"Error saving document: {str(e)}")
    
    def get_filename(self):
        """Get the document filename"""
        return self.filename
    
    def set_filename(self, filename):
        """Set the document filename"""
        self.filename = filename
    
    def get_content(self):
        """Get the document content"""
        return self.content
    
    def set_content(self, content):
        """Set the document content"""
        self.content = content
    
    def get_pdf_path(self):
        """Get the path to the compiled PDF file"""
        if not self.filename:
            return None
        
        pdf_path = os.path.splitext(self.filename)[0] + '.pdf'
        return pdf_path if os.path.exists(pdf_path) else None
    
    def get_log_path(self):
        """Get the path to the LaTeX log file"""
        if not self.filename:
            return None
        
        log_path = os.path.splitext(self.filename)[0] + '.log'
        return log_path if os.path.exists(log_path) else None
    
    def compile(self, engine="pdflatex"):
        """Compile the document with the specified LaTeX engine"""
        if not self.filename:
            raise ValueError("Cannot compile: Document has no filename")
        
        # Save the document first
        self.save()
        
        # Get the working directory
        working_dir = os.path.dirname(self.filename)
        if not working_dir:
            working_dir = os.getcwd()
        
        # Build the command
        command = [
            engine,
            "-interaction=nonstopmode",
            "-file-line-error",
            os.path.basename(self.filename)
        ]
        
        try:
            # Run the LaTeX compiler
            process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=working_dir,
                text=True
            )
            stdout, stderr = process.communicate()
            
            # Check for success (exit code 0)
            success = process.returncode == 0
            
            # Get log content
            log_content = stdout + stderr
            log_path = self.get_log_path()
            if log_path and os.path.exists(log_path):
                with open(log_path, 'r', encoding='utf-8', errors='replace') as f:
                    log_content = f.read()
            
            return success, log_content
        except Exception as e:
            return False, str(e)
    
    def get_template_content(self):
        """Get template content for a new LaTeX document"""
        return (
            "\\documentclass{article}\n"
            "\\usepackage[utf8]{inputenc}\n"
            "\\usepackage[T1]{fontenc}\n"
            "\\usepackage{lmodern}\n"
            "\\usepackage{amsmath}\n"
            "\\usepackage{amsfonts}\n"
            "\\usepackage{amssymb}\n"
            "\\usepackage{graphicx}\n\n"
            "\\title{Document Title}\n"
            "\\author{Author Name}\n"
            "\\date{\\today}\n\n"
            "\\begin{document}\n\n"
            "\\maketitle\n\n"
            "\\section{Introduction}\n"
            "This is the introduction of your document.\n\n"
            "\\section{Content}\n"
            "This is the main content of your document.\n\n"
            "\\begin{equation}\n"
            "    E = mc^2\n"
            "\\end{equation}\n\n"
            "\\section{Conclusion}\n"
            "This is the conclusion of your document.\n\n"
            "\\end{document}\n"
        )
