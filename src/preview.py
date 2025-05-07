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

class PDFPreview(Gtk.Box):
    """
    PDF Preview component for displaying compiled LaTeX documents.
    
    This class implements a preview pane that shows the PDF output
    of the compiled LaTeX document.
    """
    
    def __init__(self, window):
        """Initialize the PDF preview component."""
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        self.window = window
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
            self.scroll_window.set_child(self.webview)
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
    
    def load_pdf(self, pdf_file):
        """Load a PDF file into the preview.
        
        Args:
            pdf_file: The path to the PDF file to load
        """
        self.pdf_file = pdf_file
        
        if not os.path.exists(pdf_file):
            self.show_error("PDF file not found.")
            return
        
        if WEBKIT_AVAILABLE:
            # Create a URI for the PDF file
            uri = GLib.filename_to_uri(pdf_file, None)
            
            # Load the PDF into the WebView
            try:
                self.webview.load_uri(uri)
                self.update_zoom()
            except Exception as e:
                self.show_error(f"Error loading PDF: {e}")
        else:
            # WebKit not available, can't show PDF
            pass
    
    def show_error(self, message):
        """Show an error message in the preview.
        
        Args:
            message: The error message to display
        """
        if WEBKIT_AVAILABLE:
            error_html = f"""
            <html>
            <head>
                <style>
                    body {{ font-family: sans-serif; text-align: center; padding: 2em; }}
                    .error {{ color: red; font-weight: bold; margin: 1em; }}
                </style>
            </head>
            <body>
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
            self.window.update_status("Preview refreshed")
    
    def update_zoom(self):
        """Update the zoom level in the preview."""
        if WEBKIT_AVAILABLE:
            try:
                # Update zoom level in WebView
                self.webview.set_zoom_level(self.zoom_level)
                
                # Update the zoom label
                zoom_percent = int(self.zoom_level * 100)
                self.zoom_label.set_text(f"{zoom_percent}%")
            except Exception as e:
                print(f"Error updating zoom: {e}")
