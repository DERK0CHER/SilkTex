#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Infoscreen GUI module for Gummi.

Copyright (C) 2009-2025 Gummi Developers
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

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk

from utils import STR_EQU

# Global GUI reference (will be set in main application)
gui = None


class InfoScreenGui:
    """InfoScreen GUI handling class for Gummi."""
    
    def __init__(self, builder):
        """Initialize the InfoScreen GUI.
        
        Args:
            builder: Gtk.Builder instance with the UI definitions
        """
        # Get widgets from builder
        self.viewport = builder.get_object("preview_vport")
        self.errorpanel = builder.get_object("errorpanel")
        self.drawarea = builder.get_object("preview_draw")
        self.header = builder.get_object("error_header")
        self.image = builder.get_object("error_image")
        self.details = builder.get_object("error_details")
    
    def enable(self, msg):
        """Enable the infoscreen with a specific message.
        
        Args:
            msg: The message type to display
        """
        # In GTK4, we need to use a different approach for container manipulation
        # First remove any existing children from the viewport
        child = self.viewport.get_child()
        if child:
            self.viewport.set_child(None)
        
        # Set the appropriate message
        self.set_message(msg)
        
        # Add the error panel to the viewport
        self.viewport.set_child(self.errorpanel)
        
        # Show all widgets
        self.viewport.show()
        
        # Disable preview toolbar
        gui.previewgui.toolbar.set_sensitive(False)
    
    def disable(self):
        """Disable the infoscreen and restore normal preview."""
        # In GTK4, we can just set the child directly
        # First make sure we keep a reference to the errorpanel
        self.viewport.set_child(None)
        
        # Then add the drawarea
        self.viewport.set_child(self.drawarea)
        
        # Enable preview toolbar
        gui.previewgui.toolbar.set_sensitive(True)
    
    def set_message(self, msg):
        """Set the message to display on the infoscreen.
        
        Args:
            msg: The message type to display
        """
        self.image.set_visible(True)
        
        if STR_EQU(msg, "compile_error"):
            self.header.set_text(self._get_infoheader(1))
            self.details.set_text(self._get_infodetails(1))
        elif STR_EQU(msg, "document_error"):
            self.header.set_text(self._get_infoheader(2))
            self.details.set_text(self._get_infodetails(2))
        elif STR_EQU(msg, "program_error"):
            self.header.set_text(self._get_infoheader(3))
            self.details.set_text(self._get_infodetails(3))
        else:
            self.header.set_text(self._get_infoheader(4))
            self.details.set_text(self._get_infodetails(4))
            self.image.set_visible(False)
    
    def _get_infoheader(self, id):
        """Get the header text for a specific message ID.
        
        Args:
            id: The message ID
            
        Returns:
            The header text
        """
        if id == 1:
            return _("PDF preview could not initialise.")
        elif id == 2:
            return _("Document appears to be empty or invalid.")
        elif id == 3:
            return _("Compilation program is missing.")
        elif id == 4:
            return ""
        else:
            return "This should not have happened, bug!"
    
    def _get_infodetails(self, id):
        """Get the detailed text for a specific message ID.
        
        Args:
            id: The message ID
            
        Returns:
            The detailed text
        """
        if id == 1:
            return _(
                "The active document contains errors. The live preview\n"
                "function will resume automatically once these errors\n"
                "are resolved. Additional information is available on\n"
                "the Build log tab.\n"
            )
        elif id == 2:
            return _(
                "The document that is currently active appears to be an\n"
                "an invalid LaTeX file. You can continue working on it,\n"
                "load the default text or use the Project menu to add\n"
                "it to an active project.\n"
            )
        elif id == 3:
            return _(
                "The selected compilation program could not be located.\n"
                "Please restore the program or select an alternative\n"
                "typesetter command from the Preferences menu. The\n"
                "live preview function will not resume until Gummi\n"
                "is restarted.\n"
            )
        elif id == 4:
            return ""
        else:
            return "This should not have happened, bug!"


def infoscreengui_init(builder):
    """Initialize and return a new InfoScreenGui instance.
    
    Args:
        builder: Gtk.Builder instance with the UI definitions
        
    Returns:
        A new InfoScreenGui instance
    """
    if not isinstance(builder, Gtk.Builder):
        return None
    
    return InfoScreenGui(builder)


def infoscreengui_enable(is_gui, msg):
    """Enable the infoscreen with a specific message (wrapper function).
    
    Args:
        is_gui: The InfoScreenGui instance
        msg: The message type to display
    """
    is_gui.enable(msg)


def infoscreengui_disable(is_gui):
    """Disable the infoscreen and restore normal preview (wrapper function).
    
    Args:
        is_gui: The InfoScreenGui instance
    """
    is_gui.disable()


def infoscreengui_set_message(is_gui, msg):
    """Set the message to display on the infoscreen (wrapper function).
    
    Args:
        is_gui: The InfoScreenGui instance
        msg: The message type to display
    """
    is_gui.set_message(msg)