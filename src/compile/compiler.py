# compiler.py - LaTeX document compilation
import os
import subprocess
import threading
import tempfile
import shutil
import re
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('GLib', '2.0')
from gi.repository import GObject, GLib


class LatexCompiler(GObject.Object):
    """LaTeX document compiler"""
    
    __gsignals__ = {
        'compilation-started': (GObject.SignalFlags.RUN_FIRST, None, ()),
        'compilation-finished': (GObject.SignalFlags.RUN_FIRST, None, (bool,)),  # success
        'compilation-message': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
    }
    
    def __init__(self, config):
        """Initialize the compiler"""
        super().__init__()
        
        self.config = config
        self.compilation_in_progress = False
        self.compile_thread = None
        self.current_file = None
        self.log_content = ""
    
    def compile_document(self, filepath):
        """Compile a LaTeX document"""
        if self.compilation_in_progress:
            self.emit_message("Compilation already in progress")
            return False
        
        if not os.path.exists(filepath):
            self.emit_message(f"File not found: {filepath}")
            return False
        
        # Check if the file is a TeX file
        if not filepath.lower().endswith('.tex'):
            self.emit_message(f"Not a TeX file: {filepath}")
            return False
        
        self.compilation_in_progress = True
        self.current_file = filepath
        
        # Emit signals
        self.emit('compilation-started')
        self.emit_message(f"Compiling {os.path.basename(filepath)}...")
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(target=self._compile_thread_func)
        self.compile_thread.daemon = True
        self.compile_thread.start()
        
        return True
    
    def _compile_thread_func(self):
        """Thread function for compilation"""
        success = False
        self.log_content = ""
        
        try:
            # Get LaTeX engine
            engine = self.config.get_string('latex-engine')
            if not engine or engine not in ['pdflatex', 'xelatex', 'lualatex']:
                engine = 'pdflatex'
            
            # Check if shell escape is enabled
            shell_escape = []
            if self.config.get_boolean('latex-shell-escape'):
                shell_escape = ['-shell-escape']
            
            # Build command
            cmd = [
                engine,
                '-interaction=nonstopmode',
                '-file-line-error',
                *shell_escape,
                os.path.basename(self.current_file)
            ]
            
            # Get working directory (where the file is located)
            working_dir = os.path.dirname(self.current_file)
            if not working_dir:
                working_dir = os.getcwd()
            
            # Run LaTeX command
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=working_dir,
                text=True
            )
            
            stdout, stderr = process.communicate()
            
            # Combine output
            self.log_content = stdout
            if stderr:
                self.log_content += "\n" + stderr
            
            # Check if compilation was successful
            success = process.returncode == 0
            
            # Even with return code 0, check for errors in the log
            if success and re.search(r'(error:|fatal error|undefined control sequence|! )', self.log_content, re.IGNORECASE):
                success = False
            
            # Get messages from log
            if not success:
                error_message = self.extract_error_message(self.log_content)
                self.emit_message_in_main_thread(error_message)
            else:
                self.emit_message_in_main_thread("Compilation successful")
        
        except Exception as e:
            self.log_content += f"\nException: {str(e)}"
            self.emit_message_in_main_thread(f"Error: {str(e)}")
            success = False
        
        # Complete compilation in the main thread
        GLib.idle_add(self._compilation_completed, success)
    
    def _compilation_completed(self, success):
        """Handle compilation completion in the main thread"""
        self.compilation_in_progress = False
        
        # Emit completion signal
        self.emit('compilation-finished', success)
        
        return False  # Remove idle callback
    
    def extract_error_message(self, log_content):
        """Extract the most relevant error message from the log"""
        # Look for LaTeX errors
        error_match = re.search(r'!(.*?)[\r\n]', log_content)
        if error_match:
            return f"Error: {error_match.group(1).strip()}"
        
        # Look for file errors
        file_error = re.search(r"can't\s+find\s+(.+?)[.,]", log_content)
        if file_error:
            return f"Cannot find file: {file_error.group(1).strip()}"
        
        # Look for undefined control sequences
        undef_match = re.search(r'Undefined control sequence\.\s*\\([a-zA-Z]+)', log_content)
        if undef_match:
            return f"Undefined command: \\{undef_match.group(1)}"
        
        # Look for missing packages
        package_match = re.search(r'LaTeX Error: File `(.+?)` not found', log_content)
        if package_match:
            return f"Missing package: {package_match.group(1)}"
        
        # Generic error
        return "LaTeX compilation failed. Check the log for details."
    
    def emit_message(self, message):
        """Emit a compilation message"""
        self.emit('compilation-message', message)
    
    def emit_message_in_main_thread(self, message):
        """Emit a compilation message from the main thread"""
        GLib.idle_add(self.emit_message, message)
    
    def get_log_content(self):
        """Get the compilation log content"""
        return self.log_content

class LatexCompiler(GObject.Object):
    """Handles LaTeX document compilation"""
    
    __gsignals__ = {
        'compilation-started': (GObject.SignalFlags.RUN_FIRST, None, ()),
        'compilation-finished': (GObject.SignalFlags.RUN_FIRST, None, (bool,)),
        'compilation-progress': (GObject.SignalFlags.RUN_FIRST, None, (float,)),
        'compilation-message': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
        'compilation-error': (GObject.SignalFlags.RUN_FIRST, None, (str, int, str))
    }
    
    def __init__(self, config):
        """Initialize compiler"""
        super().__init__()
        self.config = config
        self.is_compiling = False
        self.compile_thread = None
        self.compile_process = None
        self.temp_dir = None
    
    def compile_document(self, tex_filepath, synctex=True):
        """Compile LaTeX document"""
        if self.is_compiling:
            self.emit('compilation-message', "Compilation already in progress")
            return False
        
        if not os.path.exists(tex_filepath):
            self.emit('compilation-message', f"File not found: {tex_filepath}")
            return False
        
        self.is_compiling = True
        self.emit('compilation-started')
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(
            target=self._compile_thread,
            args=(tex_filepath, synctex)
        )
        self.compile_thread.daemon = True
        self.compile_thread.start()
        
        return True
    
    def cancel_compilation(self):
        """Cancel current compilation process"""
        if self.is_compiling and self.compile_process:
            try:
                self.compile_process.terminate()
                self.emit('compilation-message', "Compilation cancelled")
            except Exception as e:
                self.emit('compilation-message', f"Error cancelling compilation: {e}")
    
    def _compile_thread(self, tex_filepath, synctex):
        """Compilation thread method"""
        try:
            # Create temporary directory for compilation
            self.temp_dir = tempfile.mkdtemp(prefix="silktex_")
            
            # Copy the TeX file and any necessary assets
            tex_dirname = os.path.dirname(tex_filepath)
            tex_basename = os.path.basename(tex_filepath)
            
            # Determine which engine to use
            engine = self.config.get_string('latex-engine')
            if not engine or engine not in ['pdflatex', 'xelatex', 'lualatex']:
                engine = 'pdflatex'
            
            # Build the command
            cmd = [engine]
            
            # Add options
            cmd.extend([
                '-interaction=nonstopmode',
                '-file-line-error'
            ])
            
            # Add synctex if requested
            if synctex:
                cmd.append('-synctex=1')
            
            # Add shell escape if configured
            if self.config.get_boolean('latex-shell-escape'):
                cmd.append('-shell-escape')
            
            # Add the tex file
            cmd.append(tex_basename)
            
            # Create the temporary working directory
            temp_work_dir = os.path.join(self.temp_dir, "work")
            os.makedirs(temp_work_dir)
            
            # Copy the TeX file and supporting files
            shutil.copy2(tex_filepath, os.path.join(temp_work_dir, tex_basename))
            
            # Also copy any files in the same directory with the same base name
            base_name = os.path.splitext(tex_basename)[0]
            for filename in os.listdir(tex_dirname):
                if filename.startswith(base_name) and filename != tex_basename:
                    src_file = os.path.join(tex_dirname, filename)
                    if os.path.isfile(src_file):
                        shutil.copy2(src_file, os.path.join(temp_work_dir, filename))
            
            # Copy any .bib, .cls, and .sty files
            for ext in ['.bib', '.cls', '.sty', '.png', '.jpg', '.jpeg', '.pdf']:
                for filename in os.listdir(tex_dirname):
                    if filename.endswith(ext):
                        src_file = os.path.join(tex_dirname, filename)
                        if os.path.isfile(src_file):
                            shutil.copy2(src_file, os.path.join(temp_work_dir, filename))
            
            # Run the compilation process
            self.emit('compilation-message', f"Compiling with {engine}...")
            
            # Run compilation twice for references
            for i in range(2):
                self.compile_process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    cwd=temp_work_dir
                )
                
                # Process output
                errors = []
                line_count = 0
                
                for line in self.compile_process.stdout:
                    line_count += 1
                    
                    # Check for errors
                    error_match = re.search(r'(.+):(\d+): (.+)', line)
                    if error_match:
                        file, line_num, error_msg = error_match.groups()
                        errors.append((file, int(line_num), error_msg))
                        # Post error to main thread
                        GLib.idle_add(
                            self.emit,
                            'compilation-error',
                            file, int(line_num), error_msg
                        )
                    
                    # Update message
                    if line_count % 10 == 0:
                        GLib.idle_add(
                            self.emit,
                            'compilation-message',
                            f"Compilation in progress... (pass {i+1}/2)"
                        )
                
                # Wait for process to complete
                self.compile_process.wait()
                
                # Update progress
                GLib.idle_add(
                    self.emit,
                    'compilation-progress',
                    (i + 1) / 2.0
                )
            
            # Check for PDF file
            pdf_file = os.path.join(temp_work_dir, f"{base_name}.pdf")
            synctex_file = os.path.join(temp_work_dir, f"{base_name}.synctex.gz")
            
            if os.path.exists(pdf_file):
                # Copy PDF back to original directory
                dest_pdf = os.path.join(tex_dirname, f"{base_name}.pdf")
                shutil.copy2(pdf_file, dest_pdf)
                
                # Copy synctex file if it exists
                if os.path.exists(synctex_file):
                    dest_synctex = os.path.join(tex_dirname, f"{base_name}.synctex.gz")
                    shutil.copy2(synctex_file, dest_synctex)
                
                GLib.idle_add(
                    self.emit,
                    'compilation-message',
                    "Compilation successful"
                )
                
                GLib.idle_add(
                    self.emit,
                    'compilation-finished',
                    True
                )
            else:
                GLib.idle_add(
                    self.emit,
                    'compilation-message',
                    "Compilation failed: No PDF produced"
                )
                
                GLib.idle_add(
                    self.emit,
                    'compilation-finished',
                    False
                )
        
        except Exception as e:
            GLib.idle_add(
                self.emit,
                'compilation-message',
                f"Compilation error: {str(e)}"
            )
            
            GLib.idle_add(
                self.emit,
                'compilation-finished',
                False
            )
        
        finally:
            self.is_compiling = False
            
            # Clean up temporary directory
            if self.temp_dir and os.path.exists(self.temp_dir):
                try:
                    shutil.rmtree(self.temp_dir)
                except Exception:
                    pass
    
    def get_log_file_path(self, tex_filepath):
        """Get path to log file for a TeX file"""
        base_name = os.path.splitext(tex_filepath)[0]
        return f"{base_name}.log"
    
    def parse_log_file(self, log_filepath):
        """Parse LaTeX log file for errors and warnings"""
        if not os.path.exists(log_filepath):
            return [], []
        
        errors = []
        warnings = []
        
        try:
            with open(log_filepath, 'r', encoding='utf-8', errors='ignore') as f:
                log_content = f.read()
            
            # Find errors
            error_pattern = r'(?:^|\n)(.*?):(\d+): (.+?)(?=\n|$)'
            for match in re.finditer(error_pattern, log_content, re.MULTILINE):
                file, line, message = match.groups()
                errors.append((file, int(line), message))
            
            # Find warnings
            warning_pattern = r'LaTeX Warning: (.+?) on (?:input )?line (\d+)'
            for match in re.finditer(warning_pattern, log_content):
                message, line = match.groups()
                warnings.append(('', int(line), message))
            
            return errors, warnings
        
        except Exception as e:
            print(f"Error parsing log file: {e}")
            return [], []
gi.require_version('Gtk', '4.0')
from gi.repository import GObject, GLib


class LatexCompiler(GObject.Object):
    """Handles the compilation of LaTeX documents to PDF"""
    
    __gsignals__ = {
        'compilation-started': (GObject.SignalFlags.RUN_FIRST, None, ()),
        'compilation-finished': (GObject.SignalFlags.RUN_FIRST, None, (bool, str)),
    }
    
    def __init__(self, config):
        super().__init__()
        
        self.config = config
        self.compile_thread = None
        self.current_file = None
        self.output_dir = None
        self.compile_in_progress = False
    
    def compile_document(self, content, file_path):
        """Compile a LaTeX document to PDF"""
        if self.compile_in_progress:
            return False
        
        self.compile_in_progress = True
        self.current_file = file_path
        
        # Emit compilation started signal
        self.emit('compilation-started')
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(target=self._compile_thread_func, args=(content, file_path))
        self.compile_thread.daemon = True
        self.compile_thread.start()
        
        return True
    
    def _compile_thread_func(self, content, file_path):
        """Thread function for LaTeX compilation"""
        success = False
        log_file = None
        
        try:
            # Create a temporary output directory
            self.output_dir = tempfile.mkdtemp(prefix="silktex_output_")
            
            # Copy the source file to the output directory
            basename = os.path.basename(file_path)
            temp_file = os.path.join(self.output_dir, basename)
            with open(temp_file, 'w') as f:
                f.write(content)
            
            # Copy any included files (like images, sty files, etc.)
            # that might be in the same directory as the source file
            source_dir = os.path.dirname(file_path)
            self._copy_dependencies(source_dir, self.output_dir, content)
            
            # Determine which LaTeX engine to use
            engine = self.config.get_string('latex-engine')
            if not engine:
                engine = 'pdflatex'
            
            # Build command with options
            cmd = [engine]
            
            # Add shell escape if enabled
            if self.config.get_boolean('latex-shell-escape'):
                cmd.append('-shell-escape')
            
            # Common options
            cmd.extend([
                '-interaction=nonstopmode',
                '-halt-on-error',
                '-file-line-error',
                basename
            ])
            
            # Run LaTeX
            log_file = os.path.join(self.output_dir, os.path.splitext(basename)[0] + '.log')
            with open(log_file, 'w') as log_out:
                proc = subprocess.Popen(
                    cmd,
                    cwd=self.output_dir,
                    stdout=log_out,
                    stderr=subprocess.STDOUT,
                    text=True
                )
                proc.wait()
                
                # Check if compilation was successful
                if proc.returncode == 0:
                    # Run bibtex if necessary
                    if '\\bibliography{' in content or '\\addbibresource{' in content:
                        self._run_bibtex(basename)
                        
                        # Run LaTeX again twice for references
                        subprocess.run(cmd, cwd=self.output_dir, stdout=log_out, stderr=subprocess.STDOUT)
                        subprocess.run(cmd, cwd=self.output_dir, stdout=log_out, stderr=subprocess.STDOUT)
                    
                    # Check if PDF was created
                    pdf_path = os.path.join(self.output_dir, os.path.splitext(basename)[0] + '.pdf')
                    if os.path.exists(pdf_path):
                        success = True
        
        except Exception as e:
            # Write exception to log file
            if log_file:
                with open(log_file, 'a') as log_out:
                    log_out.write(f"\nException during compilation: {str(e)}")
        
        # Update UI from the main thread
        GLib.idle_add(self._compilation_finished, success, log_file)
    
    def _compilation_finished(self, success, log_file):
        """Handle compilation completion (called on main thread)"""
        self.compile_in_progress = False
        
        # Emit completion signal
        self.emit('compilation-finished', success, log_file)
        
        return False  # Don't call again
    
    def _run_bibtex(self, tex_file):
        """Run BibTeX on the document"""
        base = os.path.splitext(tex_file)[0]
        subprocess.run(
            ['bibtex', base],
            cwd=self.output_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
    
    def _copy_dependencies(self, source_dir, target_dir, content):
        """Copy files that the LaTeX document depends on"""
        # This is a simplified implementation
        # A more complete version would parse the LaTeX content for \include, \input, \includegraphics, etc.
        
        # Copy all .bib files (bibliography)
        for item in os.listdir(source_dir):
            if item.endswith('.bib'):
                shutil.copy(os.path.join(source_dir, item), target_dir)
        
        # Copy image files (common formats)
        for ext in ['.png', '.jpg', '.jpeg', '.pdf', '.eps']:
            for item in os.listdir(source_dir):
                if item.lower().endswith(ext):
                    shutil.copy(os.path.join(source_dir, item), target_dir)
        
        # Copy style files
        for item in os.listdir(source_dir):
            if item.endswith('.sty') or item.endswith('.cls'):
                shutil.copy(os.path.join(source_dir, item), target_dir)
    
    def get_output_pdf_path(self):
        """Get the path to the compiled PDF file"""
        if not self.current_file or not self.output_dir:
            return None
        
        base = os.path.splitext(os.path.basename(self.current_file))[0]
        pdf_path = os.path.join(self.output_dir, base + '.pdf')
        
        if os.path.exists(pdf_path):
            return pdf_path
        
        return None
