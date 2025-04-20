import os
import sys
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
    ui = os.path.join(GUMMI_DATA, "ui", "gummi.glade")
    gtk_builder_adrom_file(builder, ui, None)
    gtk_builder_set_translation_domain(builder, C_PACKAGE)

    # Start compile thread
    if external_exists(config_get_string("Compile", "typesetter")):
        typesetter_setup()
        motion.start_compile_thread()
    else:
        infoscreengui_enable(gui.infoscreengui, "program_error")
        slog(L_ERROR, "Could not locate the typesetter program\n")

    # Install acceleration group to mainwindow
    gtk_window_add_accel_group(gui.mainwindow, snippets.accel_group)

    # tab/file loading:
    if len(sys.argv) < 2:
        tabmanager.create_tab(A_DEFAULT, None, None)
    else:
        for i in range(1, len(sys.argv)):
            argv = sys.argv[i]
            if not os.path.exists(argv):
                slog(L_ERROR, "Failed to open file '%s': No such file or directory\n", argv)
                exit(1)
            tabmanager.create_tab(A_LOAD, argv, None)

    if config_get_boolean("File", "autosaving"):
        iofunctions.start_autosave()

    gui_main(builder)
    config_save()

if __name__ == "__main__":
    main()