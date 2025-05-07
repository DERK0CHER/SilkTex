#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - A modern LaTeX editor with live preview
Main application entry point
"""

import sys
import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')

from gi.repository import Gtk, Gio, Adw

from silktex.app import SilkTexApplication


def main():
    """Main application entry point"""
    app = SilkTexApplication()
    return app.run(sys.argv)


if __name__ == '__main__':
    sys.exit(main())
