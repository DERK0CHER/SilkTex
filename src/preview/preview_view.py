    def show_error_log(self):
        """Show the LaTeX compilation error log"""
            if hasattr(self, 'error_log') and self.error_log:
                # Display the error log in the error view
                if hasattr(self, 'error_text_view'):
                    buffer = self.error_text_view.get_buffer()
                    buffer.set_text(self.error_log)
                    if hasattr(self, 'preview_stack'):
                        self.preview_stack.set_visible_child_name("error-view")
            else:
                # No errors to show
                from gi.repository import Adw
            toast = Adw.Toast(title="No compilation errors found")
            parent = self.get_root()
            if isinstance(parent, Adw.ToastOverlay):
                parent.add_toast(toast)
    
    def set_error_log(self, log_text):
        """Set the error log content"""
        self.error_log = log_text
        
        # Update the error log button visibility
        self.error_log_button.set_visible(bool(log_text))
    
    def on_load_changed(self, web_view, load_event):
        """Handle WebView load state changes"""
        if load_event == WebKit.LoadEvent.FINISHED:
            # Switch to the PDF view once loading is complete
            self.preview_stack.set_visible_child_name("pdf-view")
    
    def on_load_failed(self, web_view, load_event, failing_uri, error):
        """Handle WebView load failures"""
        # Show a message for load failure
        buffer = self.error_text_view.get_buffer()
        buffer.set_text(f"Failed to load PDF: {error.message}\nURI: {failing_uri}")
        self.preview_stack.set_visible_child_name("error-view")
        return True  # Stop other handlers
    
    def on_zoom_in_clicked(self, button):
        """Handle zoom in button click"""
        zoom_level = self.webkit_view.get_zoom_level()
        if zoom_level < 3.0:  # Maximum zoom limit
            zoom_level += 0.1
            self.webkit_view.set_zoom_level(zoom_level)
            self.update_zoom_label()
    
    def on_zoom_out_clicked(self, button):
        """Handle zoom out button click"""
        zoom_level = self.webkit_view.get_zoom_level()
        if zoom_level > 0.5:  # Minimum zoom limit
            zoom_level -= 0.1
            self.webkit_view.set_zoom_level(zoom_level)
            self.update_zoom_label()
    
    def update_zoom_label(self):
        """Update the zoom level display"""
        zoom_level = self.webkit_view.get_zoom_level() * 100
        self.zoom_level_label.set_text(f"{zoom_level:.0f}%")
    
    def on_refresh_clicked(self, button):
        """Handle refresh button click"""
        self.refresh()
    
    def on_error_log_clicked(self, button):
        """Handle error log button click"""
        self.show_error_log()
# preview_view.py - PDF preview component
import os
import gi
import tempfile
import subprocess
import threading
import time

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('WebKit', '6.0')

from gi.repository import Gtk, GLib, WebKit, GObject


class LatexPreviewView(Gtk.Box):
    """Preview component for rendering LaTeX content as PDF"""
    
    def __init__(self, config):
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        self.config = config
        
        # Create toolbar
        self.create_toolbar()
        
        # Create WebKit WebView for PDF display
        self.webview = WebKit.WebView()
        self.webview.set_vexpand(True)
        
        # Create a scroll window for the web view
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        scrolled.set_child(self.webview)
        
        # Add components to the main box
        self.append(scrolled)
        
        # Initialize variables
        self.current_file = None
        self.temp_dir = None
        self.compilation_in_progress = False
        self.latex_content = None
        self.compile_thread = None
        self.last_compile_time = 0
        self.compile_interval = self.config.get_int('preview-refresh-delay') / 1000.0
        
        # Create a temporary directory for compilation
        self.create_temp_dir()
        
        # Load default content
        self.load_default_content()
    
    def create_toolbar(self):
        """Create the toolbar with refresh and zoom controls"""
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        toolbar.set_margin_top(5)
        toolbar.set_margin_bottom(5)
        toolbar.set_margin_start(5)
        toolbar.set_margin_end(5)
        toolbar.set_spacing(5)
        
        # Refresh button
        self.refresh_button = Gtk.Button()
        self.refresh_button.set_icon_name("view-refresh-symbolic")
        self.refresh_button.set_tooltip_text("Refresh Preview")
        self.refresh_button.connect("clicked", self.on_refresh_clicked)
        toolbar.append(self.refresh_button)
        
        # Add spacing
        toolbar.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))
        
        # Zoom out button
        zoom_out_button = Gtk.Button()
        zoom_out_button.set_icon_name("zoom-out-symbolic")
        zoom_out_button.set_tooltip_text("Zoom Out")
        zoom_out_button.connect("clicked", self.on_zoom_out_clicked)
        toolbar.append(zoom_out_button)
        
        # Zoom label
        self.zoom_level = self.config.get_float('preview-zoom-level')
        self.zoom_label = Gtk.Label(label=f"{int(self.zoom_level * 100)}%")
        self.zoom_label.set_margin_start(5)
        self.zoom_label.set_margin_end(5)
        toolbar.append(self.zoom_label)
        
        # Zoom in button
        zoom_in_button = Gtk.Button()
        zoom_in_button.set_icon_name("zoom-in-symbolic")
        zoom_in_button.set_tooltip_text("Zoom In")
        zoom_in_button.connect("clicked", self.on_zoom_in_clicked)
        toolbar.append(zoom_in_button)
        
        # Add spacing
        toolbar.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))
        
        # Auto-refresh toggle
        self.auto_refresh = Gtk.CheckButton(label="Auto-refresh")
        self.auto_refresh.set_active(self.config.get_boolean('preview-auto-refresh'))
        self.auto_refresh.set_tooltip_text("Automatically refresh preview when content changes")
        toolbar.append(self.auto_refresh)
        
        # Status label (right-aligned)
        self.status_label = Gtk.Label(label="Ready")
        self.status_label.set_hexpand(True)
        self.status_label.set_halign(Gtk.Align.END)
        toolbar.append(self.status_label)
        
        self.append(toolbar)
    
    def create_temp_dir(self):
        """Create a temporary directory for preview files"""
        try:
            self.temp_dir = tempfile.mkdtemp(prefix="silktex_preview_")
            print(f"Created temporary directory: {self.temp_dir}")
        except Exception as e:
            print(f"Error creating temporary directory: {e}")
            self.temp_dir = None
    
    def load_default_content(self):
        """Load default content when no file is open"""
        default_html = """
        <html>
        <head>
            <style>
                body {
                    font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif;
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    margin: 0;
                    color: #333;
                    background-color: #f5f5f5;
                    text-align: center;
                }
                .container {
                    max-width: 80%;
                }
                h1 {
                    color: #2a76dd;
                }
                p {
                    margin: 10px 0;
                    line-height: 1.5;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>SilkTex Preview</h1>
                <p>Create or open a LaTeX document to preview it here.</p>
                <p>The preview will update automatically as you type.</p>
            </div>
        </body>
        </html>
        """
        self.webview.load_html(default_html, "file:///")
        self.status_label.set_text("Ready")
    
    def update_preview(self, latex_content, file_path=None):
        """Update the preview with new LaTeX content"""
        self.latex_content = latex_content
        self.current_file = file_path
        
        # Don't update if auto-refresh is disabled
        if not self.auto_refresh.get_active():
            self.status_label.set_text("Auto-refresh disabled")
            return
        
        # Don't compile if compilation is already in progress
        if self.compilation_in_progress:
            return
        
        # Check if enough time has passed since last compile
        current_time = time.time()
        if current_time - self.last_compile_time < self.compile_interval:
            # Schedule compilation for later
            delay_ms = int((self.last_compile_time + self.compile_interval - current_time) * 1000)
            GLib.timeout_add(delay_ms, self.quick_preview)
            return
        
        # Perform a quick preview update
        self.quick_preview()
    
    def quick_preview(self):
        """Generate a quick preview without full compilation"""
        if self.compilation_in_progress or not self.latex_content:
            return False
        
        # For now, just show the LaTeX source
        # In a full implementation, this could use a faster renderer or partial compilation
        preview_html = f"""
        <html>
        <head>
            <style>
                body {{
                    font-family: monospace;
                    padding: 20px;
                    background-color: #f5f5f5;
                    color: #333;
                }}
                pre {{
                    white-space: pre-wrap;
                    background-color: #fff;
                    border: 1px solid #ddd;
                    padding: 10px;
                    border-radius: 4px;
                }}
                .preview-note {{
                    background-color: #e8f0fe;
                    border-left: 4px solid #4285f4;
                    padding: 10px;
                    margin-bottom: 20px;
                }}
            </style>
        </head>
        <body>
            <div class="preview-note">
                <p>This is a source preview. Click the "Refresh" button or compile the document for a PDF preview.</p>
            </div>
            <pre>{self.latex_content.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')}</pre>
        </body>
        </html>
        """
        self.webview.load_html(preview_html, "file:///")
        self.status_label.set_text("Source preview - click Refresh for PDF")
        
        return False  # Don't repeat this timer
    
    def on_refresh_clicked(self, button):
        """Handle refresh button click"""
        if not self.latex_content:
            return
        
        # Full compile when the refresh button is clicked
        if not self.compilation_in_progress:
            self.compile_latex()
    
    def on_zoom_in_clicked(self, button):
        """Handle zoom in button click"""
        if self.zoom_level < 3.0:
            self.zoom_level += 0.1
            self.update_zoom()
    
    def on_zoom_out_clicked(self, button):
        """Handle zoom out button click"""
        if self.zoom_level > 0.5:
            self.zoom_level -= 0.1
            self.update_zoom()
    
    def update_zoom(self):
        """Update zoom level in the webview"""
        self.webview.set_zoom_level(self.zoom_level)
        self.zoom_label.set_text(f"{int(self.zoom_level * 100)}%")
        
        # Save to config
        self.config.set_float('preview-zoom-level', self.zoom_level)
    
    def compile_latex(self):
        """Compile LaTeX to PDF for preview"""
        if self.compilation_in_progress or not self.latex_content:
            return
        
        if not self.temp_dir:
            self.create_temp_dir()
            if not self.temp_dir:
                self.status_label.set_text("Error: Could not create temporary directory")
                return
        
        self.compilation_in_progress = True
        self.status_label.set_text("Generating preview...")
        self.refresh_button.set_sensitive(False)
        
        # Set last compile time
        self.last_compile_time = time.time()
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(target=self._compile_thread_func)
        self.compile_thread.daemon = True
        self.compile_thread.start()
    
    def _compile_thread_func(self):
        """Thread function for LaTeX compilation"""
        success = False
        error_message = None
        
        try:
            # Write the LaTeX content to a temporary file
            tex_file = os.path.join(self.temp_dir, "preview.tex")
            
            # Create a directory structure that matches the original file if available
            if self.current_file:
                # Copy any included files or graphics that might be needed
                original_dir = os.path.dirname(self.current_file)
                # In a full implementation, we would copy needed files here
            
            with open(tex_file, 'w') as f:
                f.write(self.latex_content)
            
            # Run pdflatex with the appropriate options
            engine = self.config.get_string('latex-engine')
            if not engine:
                engine = 'pdflatex'
            
            shell_escape = []
            if self.config.get_boolean('latex-shell-escape'):
                shell_escape = ['-shell-escape']
            
            cmd = [
                engine,
                '-interaction=nonstopmode',
                '-halt-on-error',
                *shell_escape,
                "preview.tex"
            ]
            
            proc = subprocess.Popen(
                cmd,
                cwd=self.temp_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            stdout, stderr = proc.communicate()
            
            if proc.returncode == 0:
                success = True
            else:
                # Extract error message from output
                import re
                error_match = re.search(r'!(.*?)[\r\n]', stdout)
                if error_match:
                    error_message = error_match.group(1).strip()
                else:
                    error_message = "Unknown error during compilation"
        
        except Exception as e:
            error_message = str(e)
        
        # Update UI from the main thread
        GLib.idle_add(self._compilation_finished, success, error_message)
    
    def _compilation_finished(self, success, error_message):
        """Handle compilation completion (called on main thread)"""
        self.compilation_in_progress = False
        self.refresh_button.set_sensitive(True)
        
        if success:
            # Load the PDF file
            pdf_path = os.path.join(self.temp_dir, "preview.pdf")
            if os.path.exists(pdf_path):
                self.load_pdf(pdf_path)
                self.status_label.set_text("Preview updated")
            else:
                self.status_label.set_text("PDF file not found")
                self.show_error("PDF file was not generated", "The compilation appeared to succeed, but no PDF file was produced.")
        else:
            self.status_label.set_text("Preview failed")
            self.show_error("LaTeX Compilation Error", error_message or "Unknown error during compilation")
    
    def load_pdf(self, pdf_path):
        """Load a PDF file into the WebView"""
        uri = f"file://{pdf_path}"
        self.webview.load_uri(uri)
        
        # Apply zoom level
        self.webview.set_zoom_level(self.zoom_level)
    
    def show_error(self, title, message):
        """Show error in the preview area"""
        error_html = f"""
        <html>
        <head>
            <style>
                body {{
                    font-family: system-ui, sans-serif;
                    padding: 20px;
                    color: #333;
                    background-color: #f8f8f8;
                }}
                .error-box {{
                    border: 1px solid #e74c3c;
                    background-color: #fdedec;
                    padding: 15px;
                    border-radius: 5px;
                    margin-bottom: 20px;
                }}
                h2 {{
                    color: #c0392b;
                    margin-top: 0;
                }}
                pre {{
                    background-color: #f1f1f1;
                    padding: 10px;
                    border-radius: 3px;
                    overflow-x: auto;
                    font-family: monospace;
                    white-space: pre-wrap;
                }}
            </style>
        </head>
        <body>
            <div class="error-box">
                <h2>{title}</h2>
                <pre>{message}</pre>
            </div>
            <p>Please fix the errors in your LaTeX document and try again.</p>
        </body>
        </html>
        """
        self.webview.load_html(error_html, "file:///")