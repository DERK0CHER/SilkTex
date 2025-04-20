#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
@file   signals.py
@brief  Define signals for Gummi

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

import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gtk, GObject


def register_gummi_signals():
    """Register custom signals for Gummi application"""
    
    # In GTK4/Python, we use GObject.signal_new instead of g_signal_new
    # Create 'document-load' signal
    GObject.signal_new(
        "document-load",             # Signal name
        GObject.GObject,             # Type to which signal belongs (GObject.GObject is base type)
        GObject.SignalFlags.RUN_FIRST,  # Signal flags
        None,                        # C return type (None = no return)
        [GObject.TYPE_PYOBJECT]      # Python object as parameter type
    )
    
    # Create 'document-write' signal
    GObject.signal_new(
        "document-write",            # Signal name
        GObject.GObject,             # Type to which signal belongs
        GObject.SignalFlags.RUN_FIRST,  # Signal flags
        None,                        # C return type (None = no return)  
        [GObject.TYPE_PYOBJECT]      # Python object as parameter type
    )


# Usage example:
if __name__ == "__main__":
    register_gummi_signals()