#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import gi
import gio
from silktex.main import main
gi.require_version('Gtk', '4.0')
from gi.repository import Gio, GLib, Gtk

GUMMI_DATA = "data"

class Gummi:
    def __init__(self):
        self.debug = False
        self.showversion = False
        self.motion = None
        self.io = None
        self.latex = None
        self.biblio = None
        self.templ = None
        self.tabm = None
        self.proj = None
        self.snippets = None

    def init(self, motion, io, latex, biblio, templ, snippets, tabm, proj):
        self.motion = motion
        self.io = io
        self.latex = latex
        self.biblio = biblio
        self.templ = templ
        self.snippets = snippets
        self.tabm = tabm
        self.proj = proj

class GuMotion:
    def __init__(self):
        pass

    def init(self, motion):
        pass

class GuIOFunc:
    def __init__(self):
        pass

    def init(self, io):
        pass

class GuLatex:
    def __init__(self):
        pass

    def init(self, latex):
        pass

class GuBiblio:
    def __init__(self):
        pass

    def init(self, biblio):
        pass

class GuTemplate:
    def __init__(self):
        pass

    def init(self, templ):
        pass

class GuTabmanager:
    def __init__(self):
        pass

    def init(self, tabm):
        pass

class GuProject:
    def __init__(self):
        pass

    def init(self, proj):
        pass

class GuSnippets:
    def __init__(self):
        pass

    def init(self, snippets):
        pass

def main():
    gummi = Gummi()

    # Initialize configuration
    config_init()

    # Initialize signals
    gummi_signals_register()

    # Initialize Classes
    motion = GuMotion()
    io = GuIOFunc()
    latex = GuLatex()
    biblio = GuBiblio()
    templ = GuTemplate()
    tabm = GuTabmanager()
    proj = GuProject()
    snippets = GuSnippets()

    gummi.init(motion, io, latex, biblio, templ, snippets, tabm, proj)

    # Initialize GUI
    builder = Gtk.Builder()
    ui = os.path.join(GUMMI_DATA, "ui", "silktex.ui")  # Changed from gummi.glade to silktex.ui
    builder.add_from_file(ui)  # Updated to use the correct method in GTK4

    # Start compile thread
    if external_exists(config_get_string("Compile", "typesetter")):
        typesetter_setup()
        motion.start_compile_thread()
    else:
        infoscreengui_enable(gui.infoscreengui, "program_error")
        slog(L_ERROR, "Could not locate the typesetter program\n")

    # Add accelerators (had to change this for GTK4)
    window = builder.get_object("SilkTexWindow")  # Get the main window (changed from mainwindow)
    if window and hasattr(snippets, 'accel_group'):
        # In GTK4, accel groups are handled differently - this will need to be updated
        pass

    # tab/file loading:
    if len(sys.argv) < 2:
        tabmanager.create_tab(A_DEFAULT, None, None)
    else:
        for i in range(1, len(sys.argv)):
            argv = sys.argv[i]
            if not os.path.exists(argv):
                slog(L_ERROR, f"Failed to open file '{argv}': No such file or directory\n")
                exit(1)
            tabmanager.create_tab(A_LOAD, argv, None)

    if config_get_boolean("File", "autosaving"):
        iofunctions.start_autosave()

    # Main application setup
    app = Gtk.Application(application_id="org.silktex.app", flags=Gio.ApplicationFlags.FLAGS_NONE)

    def on_activate(app):
        # Set up the window
        app.add_window(window)
        window.present()

    app.connect('activate', on_activate)
    app.run(sys.argv)

    config_save()

if __name__ == "__main__":
    main()
