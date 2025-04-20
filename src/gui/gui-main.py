# gui_main.py - Main GUI functionalities for the application
# Rewritten from C/GTK3 to Python/GTK4

import os
import sys
import stat
import time
from pathlib import Path
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Gdk, Gio, GLib, GtkSource, GObject

# Import application modules
from biblio import Biblio
from configfile import config_get_boolean, config_set_boolean, config_get_string, config_set_string, config_get_integer
from constants import GUMMI_DATA, PACKAGE_NAME, GUMMI_TEMPLATEDIR
from editor import Editor, editor_grab_buffer, editor_buffer_changed, editor_set_selection_textstyle, editor_insert_bib
from environment import Environment
from importer import Importer
from utils import utils_path_exists, utils_yes_no_dialog, utils_save_reload_dialog, utils_subinstr
from template import Template, template_add_new_entry, template_remove_entry, template_get_selected_path, template_create_file
from compile.rubber import Rubber
from compile.latexmk import Latexmk
from compile.texlive import texlive_active, latex_can_synctex, latex_use_synctex, latex_export_pdffile, latex_method_active

# Import GUI components
from menu_gui import MenuGui
from import_gui import ImportGui
from preview_gui import PreviewGui, previewgui_reset
from search_gui import SearchGui
from prefs_gui import PrefsGui
from snippets_gui import SnippetsGui
from tabmanager_gui import TabManagerGui, tabmanagergui_update_label
from infoscreen_gui import InfoScreenGui
from project_gui import ProjectGui
from tabmanager import tabmanager_set_active_tab, tabmanager_get_tabname, tabmanager_create_tab, tabmanager_update_tab, tabmanager_set_content

# Logging
from logger import slog, L_DEBUG, L_INFO, L_WARNING, L_ERROR, L_G_ERROR

# Constants
RECENT_FILES_NUM = 5
TYPE_LATEX = 0
TYPE_LATEX_SAVEAS = 1
TYPE_PDF = 2
TYPE_IMAGE = 3
TYPE_BIBLIO = 4
TYPE_PROJECT = 5

# Global variables (will be set in main application)
gummi = None
gui = None
g_active_tab = None
g_active_editor = None
g_e_buffer = None

class GummiGui:
    def __init__(self, builder):
        if not isinstance(builder, Gtk.Builder):
            raise TypeError("builder must be a Gtk.Builder")

        self.builder = builder

        # Get main window elements
        self.mainwindow = builder.get_object("mainwindow")
        self.toolbar = builder.get_object("maintoolbar")
        self.statusbar = builder.get_object("statusbar")
        self.rightpane = builder.get_object("box_rightpane")
        self.errorview = builder.get_object("errorview")
        self.errorbuff = self.errorview.get_buffer()
        
        # Menu items
        self.menu_spelling = builder.get_object("menu_spelling")
        self.menu_snippets = builder.get_object("menu_snippets")
        self.menu_toolbar = builder.get_object("menu_toolbar")
        self.menu_statusbar = builder.get_object("menu_statusbar")
        self.menu_rightpane = builder.get_object("menu_rightpane")
        self.menu_autosync = builder.get_object("menu_autosync")
        
        # Status bar
        self.statusid = self.statusbar.get_context_id("Gummi")
        
        # Recent files menu items
        self.recent = []
        for i in range(5):
            self.recent.append(builder.get_object(f"menu_recent{i+1}"))
        
        # Document stats window
        self.docstatswindow = builder.get_object("docstatswindow")
        
        # Bibliography compilation
        self.menu_runbibtex = builder.get_object("menu_runbibtex")
        self.menu_runmakeindex = builder.get_object("menu_runmakeindex")
        self.bibcompile = builder.get_object("bibcompile")
        
        # Initialize widgets to be (de)sensitized
        self.insens_widgets_str = [
            "box_rightpane", "tool_save", "tool_bold", "tool_italic", "tool_unline",
            "tool_left", "tool_center", "tool_right", "box_importers",
            "menu_save", "menu_saveas", "menu_close", "menu_export",
            "menu_undo", "menu_redo", "menu_cut", "menu_copy", "menu_paste",
            "menu_delete", "menu_selectall", "menu_preferences", "menu_find",
            "menu_prev", "menu_next", "menu_pdfcompile", "menu_compileopts",
            "menu_runmakeindex", "menu_runbibtex", "menu_docstat", "menu_spelling",
            "menu_snippets", "menu_edit", "menu_document", "menu_search",
            "menu_cleanup", "menu_page_layout"
        ]
        self.insens_widget_size = len(self.insens_widgets_str)
        self.insens_widgets = []
        
        for widget_name in self.insens_widgets_str:
            widget = builder.get_object(widget_name)
            if widget:
                self.insens_widgets.append(widget)
        
        # Initialize GUI components
        self.menugui = MenuGui(builder)
        self.importgui = ImportGui(builder)
        self.previewgui = PreviewGui(builder)
        self.searchgui = SearchGui(builder)
        self.prefsgui = PrefsGui(self.mainwindow)
        self.snippetsgui = SnippetsGui(self.mainwindow)
        self.tabmanagergui = TabManagerGui(builder)
        self.infoscreengui = InfoScreenGui(builder)
        self.projectgui = ProjectGui(builder)
        
        # Set icon
        icon_file = os.path.join(GUMMI_DATA, "icons", "icon.png")
        if os.path.exists(icon_file):
            self.mainwindow.set_icon_from_file(icon_file)
        
        # Set main window size and position
        if config_get_boolean("Interface", "mainwindow_max"):
            self.mainwindow.maximize()
        else:
            width = config_get_integer("Interface", "mainwindow_w")
            height = config_get_integer("Interface", "mainwindow_h")
            self.mainwindow.set_default_size(width, height)
            
            wx = config_get_integer("Interface", "mainwindow_x")
            wy = config_get_integer("Interface", "mainwindow_y")
            if wx and wy:
                self.mainwindow.set_position(Gtk.WindowPosition.NONE)
                self.mainwindow.move(wx, wy)
            else:
                self.mainwindow.set_position(Gtk.WindowPosition.CENTER)
        
        # Set up CSS for errorview
        self._setup_css()
        
        # Configure hpaned
        hpaned = builder.get_object("hpaned")
        width = self.mainwindow.get_allocated_width()
        if width > 0:
            hpaned.set_position(width // 2)
        
        # Configure menu items based on config
        if config_get_boolean("Editor", "spelling"):
            self.menu_spelling.set_active(True)
        
        if config_get_boolean("Interface", "snippets"):
            self.menu_snippets.set_active(True)
            self.menu_snippets.show()
        
        if config_get_boolean("Interface", "toolbar"):
            self.menu_toolbar.set_active(True)
            self.toolbar.show()
        else:
            config_set_boolean("Interface", "toolbar", False)
            self.menu_toolbar.set_active(False)
            self.toolbar.hide()
        
        if config_get_boolean("Interface", "statusbar"):
            self.menu_statusbar.set_active(True)
            self.statusbar.show()
        else:
            config_set_boolean("Interface", "statusbar", False)
            self.menu_statusbar.set_active(False)
            self.statusbar.hide()
        
        if config_get_boolean("Interface", "rightpane"):
            self.menu_rightpane.set_active(True)
            self.rightpane.show()
        else:
            self.menu_rightpane.set_active(False)
            self.rightpane.hide()
        
        # Configure synctex
        if latex_can_synctex() and config_get_boolean("Compile", "synctex"):
            self.menu_autosync.set_sensitive(True)
            async_enabled = latex_use_synctex()
            self.menu_autosync.set_active(async_enabled)
        
        # Set up recent files list
        self.recent_list = []
        for i in range(RECENT_FILES_NUM):
            self.recent_list.append(config_get_string("Misc", f"recent{i+1}"))
        
        self.display_recent_files()

    def _setup_css(self):
        """Set up CSS for errorview with GTK4"""
        css_provider = Gtk.CssProvider()
        css_data = """
        #errorview {
            font: 12px 'Monospace';
        }
        """
        try:
            css_provider.load_from_data(css_data.encode())
            self.errorview.get_style_context().add_provider(
                css_provider, 
                Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
            )
        except Exception as e:
            slog(L_ERROR, f"CSS error: {str(e)}")

    def display_recent_files(self):
        """Display recent files in the menu"""
        count = 0
        
        # Hide all recent menu items initially
        for i in range(5):
            self.recent[i].hide()
        
        # Show and update labels for valid recent files
        for i in range(RECENT_FILES_NUM):
            if self.recent_list[i] != "__NULL__":
                basename = os.path.basename(self.recent_list[i])
                label = f"{count + 1}. {basename}"
                self.recent[i].set_label(label)
                self.recent[i].set_tooltip_text(self.recent_list[i])
                self.recent[i].show()
                count += 1
        
        # Update config
        for i in range(RECENT_FILES_NUM):
            config_set_string("Misc", f"recent{i+1}", self.recent_list[i])

    def set_hastabs_sensitive(self, enable):
        """Enable or disable widgets based on whether there are tabs open"""
        for widget in self.insens_widgets:
            widget.set_sensitive(enable)

    def buildlog_set_text(self, message):
        """Set text in the build log output window"""
        if message:
            self.errorbuff.set_text(message, len(message))
            end_iter = self.errorbuff.get_end_iter()
            mark = self.errorbuff.create_mark(None, end_iter, False)
            self.errorview.scroll_to_mark(mark, 0.25, False, 0, 0)

    def set_window_title(self, filename, text):
        """Set the window title"""
        if filename:
            dirname = f"({os.path.dirname(filename)})"
            title = f"{text} {dirname} - {PACKAGE_NAME}"
        else:
            title = f"{text} - {PACKAGE_NAME}"
        self.mainwindow.set_title(title)


def gui_init(builder):
    """Initialize the GUI"""
    if not isinstance(builder, Gtk.Builder):
        return None
    
    return GummiGui(builder)

def gui_main(builder):
    """Start the main GUI loop"""
    # For Windows: force native looking theme
    if sys.platform == 'win32':
        settings = Gtk.Settings.get_default()
        settings.set_property("gtk-theme-name", "win32")
    
    # Connect signals
    builder.connect_signals(GtkHandlers())
    
    # Show main window
    gui.mainwindow.present()
    
    # Start GTK main loop
    Gtk.main()

def set_filename_display(tc, update_title=True, update_label=True):
    """Update filename display in tab and window title"""
    filetext = tabmanager_get_tabname(tc)
    
    if update_label:
        tabmanagergui_update_label(tc.page, filetext)
    if update_title:
        gui.set_window_title(tc.editor.filename, filetext)

def recovery_mode_enable(tab, filename):
    """Enable recovery mode for the given tab"""
    prev_workfile = get_swapfile(filename)
    
    slog(L_WARNING, f"Swap file `{prev_workfile}' found.")
    msg = f"Swap file exists for {filename}, do you want to recover from it?"
    tab.page.barlabel.set_text(msg)
    
    data = filename
    tab.page.infosignal = tab.page.infobar.connect(
        "response", 
        on_recovery_infobar_response, 
        data
    )
    
    tab.editor.view.set_sensitive(False)
    tab.page.infobar.show()

def recovery_mode_disable(infobar):
    """Disable recovery mode"""
    infobar.disconnect(g_active_tab.page.infosignal)
    infobar.hide()
    g_active_editor.view.set_sensitive(True)

def open_file(filename):
    """Open a file in the editor"""
    if not os.path.exists(filename):
        slog(L_G_ERROR, f"Failed to open file '{filename}': No such file or directory")
        return
    
    tabmanager_create_tab("A_LOAD", filename, None)
    if not gui.rightpane.get_sensitive():
        gui.set_hastabs_sensitive(True)

def save_file(tab, saveas=False):
    """Save the current file"""
    is_new = False
    filename = None
    pdfname = None
    prev = None
    ret = 0
    
    if saveas or not tab.editor.filename:
        filename = get_save_filename(TYPE_LATEX)
        if filename:
            is_new = True
            if not filename.endswith(".tex"):
                filename = f"{filename}.tex"
            
            if utils_path_exists(filename):
                ret = utils_yes_no_dialog("The file already exists. Overwrite?")
                if ret != Gtk.ResponseType.YES:
                    return
        else:
            return
    else:
        filename = tab.editor.filename
    
    # Check whether the file has been changed by an external program
    try:
        file_stat = os.stat(filename)
        lastmod = tab.editor.last_modtime - file_stat.st_mtime
        
        if lastmod != 0.0 and tab.editor.last_modtime != 0.0:
            ret = utils_save_reload_dialog(
                "The content of the file has been changed externally. "
                "Saving will remove any external modifications."
            )
            if ret == Gtk.ResponseType.YES:
                tabmanager_set_content("A_LOAD", filename, None)
                file_stat = os.stat(filename)
                tab.editor.last_modtime = file_stat.st_mtime
                return
            elif ret != Gtk.ResponseType.NO:
                # Cancel - do nothing
                return
    except OSError:
        pass  # File might not exist yet
    
    focus = gui.mainwindow.get_focus()
    text = editor_grab_buffer(tab.editor)
    
    if focus:
        focus.grab_focus()
    
    # Actually save the file
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(text)
    
    # Export to PDF if configured
    if config_get_boolean("File", "autoexport"):
        pdfname = filename[:-4]  # Remove .tex
        latex_export_pdffile(gummi.latex, tab.editor, pdfname, False)
    
    if is_new:
        tabmanager_update_tab(filename)
    
    set_filename_display(tab, True, True)
    tab.editor.view.grab_focus()
    
    # Update modification time
    try:
        file_stat = os.stat(filename)
        tab.editor.last_modtime = file_stat.st_mtime
    except OSError:
        pass

def add_to_recent_list(filename):
    """Add a file to the recent files list"""
    if not filename:
        return
    
    # Check if it already exists
    for i in range(5):
        if filename == gui.recent_list[i]:
            return
    
    # Add to recent list by shifting
    gui.recent_list.pop()
    gui.recent_list.insert(0, filename)
    gui.display_recent_files()

def check_for_save(editor):
    """Check if there are unsaved changes"""
    if editor and editor_buffer_changed(editor):
        basename = os.path.basename(editor.filename) if editor.filename else "this document"
        dialog = Gtk.MessageDialog(
            transient_for=gui.mainwindow,
            flags=Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
            message_type=Gtk.MessageType.QUESTION,
            buttons=Gtk.ButtonsType.NONE,
            text="This document has unsaved changes"
        )
        
        dialog.format_secondary_text(
            f"Do you want to save the changes to {basename} before closing?"
        )
        
        dialog.add_buttons(
            "_Close without Saving", Gtk.ResponseType.NO,
            "_Cancel", Gtk.ResponseType.CANCEL,
            "_Save As", Gtk.ResponseType.YES,
            None
        )
        
        dialog.set_default_response(Gtk.ResponseType.YES)
        
        ret = dialog.run()
        dialog.destroy()
        return ret
    
    return 0

def get_open_filename(filetype):
    """Display a file chooser dialog for opening files"""
    active_cwd = None
    
    if g_active_editor and g_active_editor.filename:
        active_cwd = os.path.dirname(g_active_editor.filename)
    
    chooser_titles = {
        TYPE_LATEX: "Open LaTeX document",
        TYPE_IMAGE: "Select an image to insert",
        TYPE_BIBLIO: "Select bibliography file",
        TYPE_PROJECT: "Select project file"
    }
    
    dialog = Gtk.FileChooserDialog(
        title=chooser_titles.get(filetype, "Open file"),
        parent=gui.mainwindow,
        action=Gtk.FileChooserAction.OPEN
    )
    
    dialog.add_buttons(
        "_Cancel", Gtk.ResponseType.CANCEL,
        "_Open", Gtk.ResponseType.OK
    )
    
    file_dialog_set_filter(dialog, filetype)
    
    if active_cwd:
        dialog.set_current_folder(Gio.File.new_for_path(active_cwd))
    else:
        dialog.set_current_folder(Gio.File.new_for_path(os.path.expanduser("~")))
    
    filename = None
    if dialog.run() == Gtk.ResponseType.OK:
        filename = dialog.get_file().get_path()
    
    dialog.destroy()
    return filename

def get_save_filename(filetype):
    """Display a file chooser dialog for saving files"""
    chooser_titles = {
        TYPE_LATEX: "Save LaTeX document",
        TYPE_LATEX_SAVEAS: "Save as LaTeX document",
        TYPE_PDF: "Export to PDF",
        TYPE_PROJECT: "Create project"
    }
    
    dialog = Gtk.FileChooserDialog(
        title=chooser_titles.get(filetype, "Save file"),
        parent=gui.mainwindow,
        action=Gtk.FileChooserAction.SAVE
    )
    
    dialog.add_buttons(
        "_Cancel", Gtk.ResponseType.CANCEL,
        "_Save", Gtk.ResponseType.OK
    )
    
    dialog.set_do_overwrite_confirmation(True)
    file_dialog_set_filter(dialog, filetype)
    dialog.set_current_folder(Gio.File.new_for_path(os.path.expanduser("~")))
    
    if g_active_editor.filename:
        dirname = os.path.dirname(g_active_editor.filename)
        basename = os.path.basename(g_active_editor.filename)
        
        dialog.set_current_folder(Gio.File.new_for_path(dirname))
        
        if filetype == TYPE_PDF:
            pdf_name = f"{basename[:-4]}.pdf"
            dialog.set_current_name(pdf_name)
        elif filetype == TYPE_LATEX_SAVEAS:
            dialog.set_current_name(basename)
    else:
        unsaved = "Unsaved Document"
        if filetype == TYPE_PDF:
            unsaved = f"{unsaved}.pdf"
        if filetype == TYPE_LATEX:
            unsaved = f"{unsaved}.tex"
        dialog.set_current_name(unsaved)
    
    filename = None
    if dialog.run() == Gtk.ResponseType.OK:
        filename = dialog.get_file().get_path()
    
    dialog.destroy()
    return filename

def file_dialog_set_filter(dialog, filetype):
    """Set file filters for file chooser dialogs"""
    filter_obj = Gtk.FileFilter()
    
    if filetype in [TYPE_LATEX, TYPE_LATEX_SAVEAS]:
        filter_obj.set_name("LaTeX files")
        filter_obj.add_pattern("*.tex")
    elif filetype == TYPE_PDF:
        filter_obj.set_name("PDF files")
        filter_obj.add_pattern("*.pdf")
    elif filetype == TYPE_IMAGE:
        filter_obj.set_name("Supported Image files")
        
        # Pdflatex supports different formats than pure latex
        if latex_method_active("texpdf"):
            filter_obj.add_pattern("*.jpg")
            filter_obj.add_pattern("*.jpeg")
            filter_obj.add_pattern("*.png")
            filter_obj.add_pattern("*.pdf")
        filter_obj.add_pattern("*.eps")
    elif filetype == TYPE_BIBLIO:
        filter_obj.set_name("Bibtex files")
        filter_obj.add_pattern("*.bib")
    elif filetype == TYPE_PROJECT:
        filter_obj.set_name("Gummi project files")
        filter_obj.add_pattern("*.gummi")
    
    dialog.add_filter(filter_obj)
    dialog.set_filter(filter_obj)

def statusbar_set_message(message):
    """Set a message in the statusbar"""
    if not gui.statusbar:
        return
    
    context_id = gui.statusbar.get_context_id("Gummi")
    gui.statusbar.push(context_id, message)
    GLib.timeout_add_seconds(4, statusbar_del_message, None)

def statusbar_del_message(user_data):
    """Clear the statusbar message"""
    if gui.statusbar:
        context_id = gui.statusbar.get_context_id("Gummi")
        gui.statusbar.pop(context_id)
    return False

def typesetter_setup():
    """Set up the typesetter"""
    # Change the pref gui options based on active typesetter
    status = texlive_active()
    gui.menu_runbibtex.set_sensitive(status)
    gui.menu_runmakeindex.set_sensitive(status)
    gui.prefsgui.opt_shellescape.set_sensitive(status)
    
    # Set synctex state
    if config_get_boolean("Compile", "synctex"):
        gui.prefsgui.opt_synctex.set_active(True)
    else:
        gui.prefsgui.opt_synctex.set_active(False)
    
    gui.prefsgui.opt_synctex.set_sensitive(True)
    
    slog(L_INFO, f"Typesetter {config_get_string('Compile', 'typesetter')} configured")

def check_preview_timer():
    """Check if preview timer should be started"""
    global g_active_tab, g_e_buffer
    
    if not g_active_tab:
        return
    
    g_e_buffer.set_modified(True)
    gummi.latex.modified_since_compile = True
    
    set_filename_display(g_active_tab, True, True)
    
    # Start motion timer
    gummi.motion.start_timer()


class GtkHandlers:
    """Handler class for GTK signals"""
    
    def on_menu_autosync_toggled(self, menu_autosync, user_data=None):
        """Handle autosync menu item toggle"""
        if menu_autosync.get_active():
            config_set_boolean("Preview", "autosync", True)
        else:
            config_set_boolean("Preview", "autosync", False)
    
    def on_tab_notebook_switch_page(self, notebook, page, page_num, user_data=None):
        """Handle notebook tab switch"""
        global g_active_tab
        
        slog(L_DEBUG, f"Switched to environment at page {page_num}")
        # Kill typesetter command
        gummi.motion.kill_typesetter()
        
        # Set active tab/editor pointers
        tabmanager_set_active_tab(page_num)
        
        # Update window title
        set_filename_display(g_active_tab, True, False)
        
        # Clear build log
        gui.buildlog_set_text("")
        
        # Reset preview
        previewgui_reset(gui.previewgui)
    
    def on_right_notebook_switch_page(self, notebook, page, page_num, user_data=None):
        """Handle right notebook tab switch"""
        if page_num == 2:  # Project tab
            if gummi.project_active():
                pass  # projectgui_enable(gummi.project, gui.projectgui)
            else:
                pass  # projectgui_disable(gummi.project, gui.projectgui)
    
    def on_menu_bibupdate_activate(self, widget, user_data=None):
        """Handle bibliography update menu activation"""
        gummi.biblio.compile_bibliography(g_active_editor)
    
    def on_docstats_close_clicked(self, widget, user_data=None):
        """Handle close button for document stats window"""
        gui.docstatswindow.hide()
        return True
    
    def on_tool_textstyle_bold_activate(self, widget, user_data=None):
        """Handle bold text style button"""
        editor_set_selection_textstyle(g_active_editor, "tool_bold")
    
    def on_tool_textstyle_italic_activate(self, widget, user_data=None):
        """Handle italic text style button"""
        editor_set_selection_textstyle(g_active_editor, "tool_italic")
    
    def on_tool_textstyle_underline_activate(self, widget, user_data=None):
        """Handle underline text style button"""
        editor_set_selection_textstyle(g_active_editor, "tool_unline")
    
    def on_tool_textstyle_left_activate(self, widget, user_data=None):
        """Handle align left text style button"""
        editor_set_selection_textstyle(g_active_editor, "tool_left")
    
    def on_tool_textstyle_center_activate(self, widget, user_data=None):
        """Handle align center text style button"""
        editor_set_selection_textstyle(g_active_editor, "tool_center")
    
    def on_tool_textstyle_right_activate(self, widget, user_data=None):
        """Handle align right text style button"""
        editor_set_selection_textstyle(g_active_editor, "tool_right")
    
    def on_button_template_add_clicked(self, widget, user_data=None):
        """Handle add template button"""
        template_add_new_entry(gummi.templ)
    
    def on_button_template_remove_clicked(self, widget, user_data=None):
        """Handle remove template button"""
        template_remove_entry(gummi.templ)
    
    def on_button_template_open_clicked(self, widget, user_data=None):
        """Handle open template button"""
        templ_name = template_get_selected_path(gummi.templ)
        
        if templ_name:
            # Add loading message to status bar
            statusbar_set_message("Loading template ...")
            
            tabmanager_create_tab("A_LOAD_OPT", None, templ_name)
            gummi.templ.templatewindow.hide()
        
        if not gui.rightpane.get_sensitive():
            gui.set_hastabs_sensitive(True)
    
    def on_button_template_close_clicked(self, widget, user_data=None):
        """Handle close template button"""
        gummi.templ.template_add.set_sensitive(True)
        gummi.templ.template_remove.set_sensitive(True)
        gummi.templ.template_open.set_sensitive(True)
        gummi.templ.templatewindow.hide()
    
    def on_template_rowitem_edited(self, widget, path, filenm, user_data=None):
        """Handle template row item edit"""
        filepath = os.path.join(GUMMI_TEMPLATEDIR, filenm)
        
        model = gummi.templ.templateview.get_model()
        selection = gummi.templ.templateview.get_selection()
        
        iter_obj = None
        if selection.get_selected():
            iter_obj = selection.get_selected()[1]
            
            if iter_obj:
                gummi.templ.list_templates.set(iter_obj, 0, filenm, 1, filepath)
                text = editor_grab_buffer(g_active_editor)
                template_create_file(gummi.templ, filenm, text)
    
    def on_template_cursor_changed(self, tree, user_data=None):
        """Handle template cursor change"""
        if tree.get_selection():
            gummi.templ.template_open.set_sensitive(True)
    
    def on_bibcolumn_clicked(self, widget, user_data=None):
        """Handle bibliography column click"""
        column_id = widget