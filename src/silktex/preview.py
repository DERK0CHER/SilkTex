#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - PDF Preview module

This module implements the PDF preview component for displaying
compiled LaTeX documents.
"""

import os
import gi
import tempfile

gi.require_version('Gtk', '4.0')
try:
    gi.require_version('WebKit', '6.0')
    WEBKIT_AVAILABLE = True
except ValueError:
    try:
        gi.require_version('WebKit2', '4.0')
        WEBKIT_AVAILABLE = True
    except ValueError:
        WEBKIT_AVAILABLE = False

from gi.repository import Gtk, GLib, Gio

if WEBKIT_AVAILABLE:
    try:
        from gi.repository import WebKit
    except ImportError:
        from gi.repository import WebKit2 as WebKit

class PdfPreview(Gtk.Box):
    """
    PDF Preview component for displaying compiled LaTeX documents.
    
    This class implements a preview pane that shows the PDF output
    of the compiled LaTeX document.
    """
    
    def __init__(self):
        """Initialize the PDF preview component."""
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        self.pdf_file = None
        self.zoom_level = 1.0
        
        self.setup_ui()
    
    def setup_ui(self):
        """Set up the preview UI components."""
        # Create toolbar for preview controls
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        toolbar.add_css_class("toolbar")
        toolbar.set_spacing(8)
        toolbar.set_margin_start(8)
        toolbar.set_margin_end(8)
        toolbar.set_margin_top(8)
        toolbar.set_margin_bottom(8)
        
        # Zoom out button
        zoom_out_button = Gtk.Button.new_from_icon_name("zoom-out-symbolic")
        zoom_out_button.set_tooltip_text("Zoom Out")
        zoom_out_button.connect("clicked", self.on_zoom_out)
        toolbar.append(zoom_out_button)
        
        # Zoom level label
        self.zoom_label = Gtk.Label.new("100%")
        self.zoom_label.set_width_chars(6)
        toolbar.append(self.zoom_label)
        
        # Zoom in button
        zoom_in_button = Gtk.Button.new_from_icon_name("zoom-in-symbolic")
        zoom_in_button.set_tooltip_text("Zoom In")
        zoom_in_button.connect("clicked", self.on_zoom_in)
        toolbar.append(zoom_in_button)
        
        # Reset zoom button
        reset_zoom_button = Gtk.Button.new_from_icon_name("zoom-original-symbolic")
        reset_zoom_button.set_tooltip_text("Reset Zoom")
        reset_zoom_button.connect("clicked", self.on_reset_zoom)
        toolbar.append(reset_zoom_button)
        
        # Spacer
        spacer = Gtk.Box()
        spacer.set_hexpand(True)
        toolbar.append(spacer)
        
        # Refresh button
        refresh_button = Gtk.Button.new_from_icon_name("view-refresh-symbolic")
        refresh_button.set_tooltip_text("Refresh")
        refresh_button.connect("clicked", self.on_refresh)
        toolbar.append(refresh_button)
        
        self.append(toolbar)
        
        # Create the preview content area
        self.create_preview_widget()
    
    def create_preview_widget(self):
        """Create the widget for displaying PDFs."""
        # Create a ScrolledWindow to contain the preview
        self.scroll_window = Gtk.ScrolledWindow()
        self.scroll_window.set_hexpand(True)
        self.scroll_window.set_vexpand(True)
        
        # Use WebKit if available, otherwise use a fallback message
        if WEBKIT_AVAILABLE:
            self.webview = WebKit.WebView()
            
            # Configure settings
            settings = self.webview.get_settings()
            settings.set_javascript_can_access_clipboard(True)
            
            self.scroll_window.set_child(self.webview)
            self.set_empty_view()
        else:
            # Fallback message
            content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            content_box.set_halign(Gtk.Align.CENTER)
            content_box.set_valign(Gtk.Align.CENTER)
            
            icon = Gtk.Image.new_from_icon_name("dialog-error-symbolic")
            icon.set_pixel_size(48)
            content_box.append(icon)
            
            label = Gtk.Label.new("WebKit is not available.\nPDF preview is not supported.")
            label.set_margin_top(12)
            content_box.append(label)
            
            self.scroll_window.set_child(content_box)
        
        self.append(self.scroll_window)
    
    def set_empty_view(self):
        """Show empty preview with message"""
        if not WEBKIT_AVAILABLE:
            return
            
        empty_html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="utf-8">
            <style>
                body { 
                    margin: 0; 
                    padding: 0; 
                    background-color: #f0f0f0;
                    font-family: sans-serif;
                }
                #placeholder { 
                    display: flex; 
                    height: 100vh; 
                    justify-content: center; 
                    align-items: center; 
                    color: #666; 
                    font-size: 1.2em;
                    flex-direction: column;
                }
                .icon {
                    font-size: 48px;
                    margin-bottom: 20px;
                    color: #999;
                }
            </style>
        </head>
        <body>
            <div id="placeholder">
                <div class="icon">📄</div>
                <div>PDF preview will appear here after compilation</div>
                <div style="font-size: 0.8em; margin-top: 10px;">Compile your document to see the results</div>
            </div>
        </body>
        </html>
        """
        self.webview.load_html(empty_html, "file:///")
    
    def load_pdf(self, pdf_file):
        """Load a PDF file into the preview.
        
        Args:
            pdf_file: The path to the PDF file to load
        """
        if not WEBKIT_AVAILABLE:
            return
            
        self.pdf_file = pdf_file
        
        if not os.path.exists(pdf_file):
            self.show_error("PDF file not found.")
            return
        
        # Create a URI for the PDF file
        uri = GLib.filename_to_uri(pdf_file, None)
        
        # Create HTML to embed the PDF
        embed_html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="utf-8">
            <style>
                body {{ margin: 0; padding: 0; background-color: #f0f0f0; }}
                #pdf-container {{ width: 100%; height: 100vh; }}
            </style>
        </head>
        <body>
            <embed 
                id="pdf-container" 
                src="{uri}"
                type="application/pdf" 
                width="100%" 
                height="100%" 
            />
        </body>
        </html>
        """
        
        # Load the HTML into the WebView
        try:
            self.webview.load_html(embed_html, "file:///")
            self.update_zoom()
        except Exception as e:
            self.show_error(f"Error loading PDF: {str(e)}")
    
    def show_error(self, message):
        """Show an error message in the preview.
        
        Args:
            message: The error message to display
        """
        if not WEBKIT_AVAILABLE:
            return
            
        error_html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <style>
                body {{ 
                    font-family: sans-serif; 
                    text-align: center; 
                    padding: 2em;
                    background-color: #f0f0f0;
                }}
                .error {{ 
                    color: red; 
                    font-weight: bold; 
                    margin: 1em; 
                    padding: 1em;
                    background-color: #fff;
                    border-radius: 4px;
                    box-shadow: 0 2px 4px rgba(0,0,0,0.1);
                }}
                .icon {{
                    font-size: 48px;
                    margin-bottom: 20px;
                }}
            </style>
        </head>
        <body>
            <div class="icon">❌</div>
            <h2>Preview Error</h2>
            <div class="error">{message}</div>
        </body>
        </html>
        """
        self.webview.load_html(error_html, "file:///")
    
    def on_zoom_in(self, button):
        """Handle zoom in button click."""
        self.zoom_level = min(self.zoom_level + 0.1, 3.0)
        self.update_zoom()
    
    def on_zoom_out(self, button):
        """Handle zoom out button click."""
        self.zoom_level = max(self.zoom_level - 0.1, 0.5)
        self.update_zoom()
    
    def on_reset_zoom(self, button):
        """Handle reset zoom button click."""
        self.zoom_level = 1.0
        self.update_zoom()
    
    def on_refresh(self, button):
        """Handle refresh button click."""
        if self.pdf_file and os.path.exists(self.pdf_file):
            self.load_pdf(self.pdf_file)
    
    def update_zoom(self):
        """Update the zoom level in the preview."""
        if not WEBKIT_AVAILABLE:
            return
            
        try:
            # Update zoom level in WebView
            self.webview.set_zoom_level(self.zoom_level)
            
            # Update the zoom label
            zoom_percent = int(self.zoom_level * 100)
            self.zoom_label.set_text(f"{zoom_percent}%")
        except Exception as e:
            print(f"Error updating zoom: {e}")
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - PDF Preview module

This module provides PDF preview functionality for LaTeX documents
using WebKit or PDF rendering libraries.
"""

import os
import gi
import tempfile
import logging

# Try to import WebKit for PDF rendering
WEBKIT_AVAILABLE = False
try:
    gi.require_version('WebKit', '6.0')
    from gi.repository import WebKit
    WEBKIT_AVAILABLE = True
except (ImportError, ValueError):
    # WebKit not available, try alternative
    try:
        gi.require_version('WebKit2', '4.1')
        from gi.repository import WebKit2 as WebKit
        WEBKIT_AVAILABLE = True
    except (ImportError, ValueError):
        # WebKit still not available
        pass

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gdk, GLib, GObject


class PDFPreview(Gtk.Box):
    """PDF Preview component for LaTeX documents"""
    
    def __init__(self):
        """Initialize the PDF preview component"""
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        # Initialize properties
        self.pdf_file = None
        self.zoom_level = 1.0
        
        # Set up UI components
        self.create_ui()
    
    def create_ui(self):
        """Create UI components"""
        # Create toolbar for preview actions
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        toolbar.add_css_class("toolbar-view")
        toolbar.set_spacing(6)
        toolbar.set_margin_start(6)
        toolbar.set_margin_end(6)
        toolbar.set_margin_top(6)
        toolbar.set_margin_bottom(6)
        
        # Add zoom out button
        zoom_out_button = Gtk.Button.new_from_icon_name("zoom-out-symbolic")
        zoom_out_button.set_tooltip_text("Zoom Out")
        zoom_out_button.add_css_class("toolbar-button")
        zoom_out_button.connect("clicked", self.on_zoom_out)
        toolbar.append(zoom_out_button)
        
        # Add zoom level label
        self.zoom_label = Gtk.Label.new("100%")
        self.zoom_label.set_margin_start(6)
        self.zoom_label.set_margin_end(6)
        toolbar.append(self.zoom_label)
        
        # Add zoom in button
        zoom_in_button = Gtk.Button.new_from_icon_name("zoom-in-symbolic")
        zoom_in_button.set_tooltip_text("Zoom In")
        zoom_in_button.add_css_class("toolbar-button")
        zoom_in_button.connect("clicked", self.on_zoom_in)
        toolbar.append(zoom_in_button)
        
        # Add reset zoom button
        zoom_reset_button = Gtk.Button.new_from_icon_name("zoom-original-symbolic")
        zoom_reset_button.set_tooltip_text("Reset Zoom")
        zoom_reset_button.add_css_class("toolbar-button")
        zoom_reset_button.connect("clicked", self.on_zoom_reset)
        toolbar.append(zoom_reset_button)
        
        # Add space between zoom controls and other buttons
        spacer = Gtk.Box()
        spacer.set_hexpand(True)
        toolbar.append(spacer)
        
        # Add reload button
        reload_button = Gtk.Button.new_from_icon_name("view-refresh-symbolic")
        reload_button.set_tooltip_text("Reload PDF")
        reload_button.add_css_class("toolbar-button")
        reload_button.connect("clicked", self.on_reload)
        toolbar.append(reload_button)
        
        # Add open button
        open_button = Gtk.Button.new_from_icon_name("document-open-symbolic")
        open_button.set_tooltip_text("Open in External Viewer")
        open_button.add_css_class("toolbar-button")
        open_button.connect("clicked", self.on_open_external)
        toolbar.append(open_button)
        
        self.append(toolbar)
        
        # Create preview container
        self.preview_container = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.preview_container.set_hexpand(True)
        self.preview_container.set_vexpand(True)
        self.preview_container.add_css_class("preview-container")
        
        # Create preview content based on available libraries
        if WEBKIT_AVAILABLE:
            self.create_webkit_preview()
        else:
            self.create_fallback_preview()
        
        self.append(self.preview_container)
    
    def create_webkit_preview(self):
        """Create WebKit-based PDF preview"""
        # Create WebKit settings
        settings = WebKit.Settings()
        settings.set_enable_javascript(True)
        settings.set_enable_developer_extras(False)
        
        # Create WebKit webview
        self.webview = WebKit.WebView.new_with_settings(settings)
        self.webview.set_hexpand(True)
        self.webview.set_vexpand(True)
        
        # Add webview to container
        scrolled_window = Gtk.ScrolledWindow()
        scrolled_window.set_hexpand(True)
        scrolled_window.set_vexpand(True)
        scrolled_window.set_child(self.webview)
        
        self.preview_container.append(scrolled_window)
        
        # Show a blank page initially
        self.webview.load_html("<html><body><p>Compile your document to see the preview.</p></body></html>", "about:blank")
    
    def create_fallback_preview(self):
        """Create fallback preview when WebKit is not available"""
        # Create a label for message
        label = Gtk.Label.new("PDF preview requires WebKit library.\n\nPlease install the WebKit GTK library to enable preview functionality.")
        label.set_justify(Gtk.Justification.CENTER)
        label.set_margin_top(24)
        label.set_margin_bottom(24)
        
        self.preview_container.append(label)
    
    def load_pdf(self, pdf_file):
        """Load a PDF file into the preview.
        
        Args:
            pdf_file: The path to the PDF file to load
        """
        if not WEBKIT_AVAILABLE:
            return
            
        self.pdf_file = pdf_file
        
        if not os.path.exists(pdf_file):
            self.show_error("PDF file not found.")
            return
        
        # Create a URI for the PDF file
        uri = GLib.filename_to_uri(pdf_file, None)
        
        # Create HTML to embed the PDF
        embed_html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="utf-8">
            <style>
                body {{ margin: 0; padding: 0; background-color: #f0f0f0; }}
                #pdf-container {{ width: 100%; height: 100vh; }}
            </style>
        </head>
        <body>
            <embed 
                id="pdf-container" 
                src="{uri}"
                type="application/pdf" 
                width="100%" 
                height="100%" 
            />
        </body>
        </html>
        """
        
        # Load the HTML into the WebView
        try:
            self.webview.load_html(embed_html, "file:///")
            self.update_zoom()
        except Exception as e:
            self.show_error(f"Error loading PDF: {str(e)}")
    
    def show_error(self, message):
        """Show an error message in the preview
        
        Args:
            message: Error message to display
        """
        if not WEBKIT_AVAILABLE:
            return
            
        error_html = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="utf-8">
            <style>
                body {{ 
                    margin: 0; 
                    padding: 20px; 
                    font-family: sans-serif; 
                    background-color: #f0f0f0; 
                    color: #cc0000;
                }}
                .error-container {{ 
                    border: 1px solid #cc0000; 
                    border-radius: 5px; 
                    padding: 20px; 
                    margin-top: 20px;
                    background-color: #fff0f0; 
                }}
                h2 {{ margin-top: 0; }}
            </style>
        </head>
        <body>
            <div class="error-container">
                <h2>Preview Error</h2>
                <p>{message}</p>
            </div>
        </body>
        </html>
        """
        
        self.webview.load_html(error_html, "about:blank")
    
    def update_zoom(self):
        """Update the zoom level of the PDF preview"""
        if not WEBKIT_AVAILABLE or not hasattr(self, 'webview'):
            return
            
        # Update zoom level
        self.webview.set_zoom_level(self.zoom_level)
        
        # Update zoom label
        zoom_percent = int(self.zoom_level * 100)
        self.zoom_label.set_text(f"{zoom_percent}%")
    
    def on_zoom_in(self, button):
        """Handle zoom in button click"""
        self.zoom_level = min(3.0, self.zoom_level + 0.1)
        self.update_zoom()
    
    def on_zoom_out(self, button):
        """Handle zoom out button click"""
        self.zoom_level = max(0.5, self.zoom_level - 0.1)
        self.update_zoom()
    
    def on_zoom_reset(self, button):
        """Handle zoom reset button click"""
        self.zoom_level = 1.0
        self.update_zoom()
    
    def on_reload(self, button):
        """Handle reload button click"""
        if self.pdf_file and os.path.exists(self.pdf_file):
            self.load_pdf(self.pdf_file)
    
    def on_open_external(self, button):
        """Handle open in external viewer button click"""
        if not self.pdf_file or not os.path.exists(self.pdf_file):
            if hasattr(self.get_root(), "show_toast"):
                self.get_root().show_toast("No PDF file to open")
            return
            
        try:
            # Use GIO to open the PDF with the default application
            launcher = Gtk.FileLauncher.new_for_file(Gio.File.new_for_path(self.pdf_file))
            launcher.launch(None, None)
        except Exception as e:
            logging.error(f"Error opening PDF in external viewer: {str(e)}")
            if hasattr(self.get_root(), "show_toast"):
                self.get_root().show_toast(f"Error opening PDF: {str(e)}")