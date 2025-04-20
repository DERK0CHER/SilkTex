#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Preferences GUI module for Gummi.

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

import os
import subprocess
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GLib, Gdk, GtkSource, Pango

from constants import GUMMI_DATA, C_PACKAGE, C_TMPDIR, C_WELCOMETEXT, C_DEFAULTTEXT
from configfile import (config_get_boolean, config_get_integer, config_get_string, 
                       config_set_boolean, config_set_integer, config_set_string,
                       config_value_as_str_equals, config_load_defaults,
                       config_save)
from environment import Environment
from gui.gui_main import GummiGui
from latex import (pdflatex_detected, xelatex_detected, lualatex_detected,
                 pdflatex_active, xelatex_active, lualatex_active,
                 rubber_detected, rubber_active, latexmk_detected, latexmk_active,
                 latex_method_active, latex_use_shellescaping, latex_can_synctex,
                 typesetter_setup)
from utils import (utils_popen_r, utils_copy_file, utils_set_file_contents,
                  utils_pango_font_desc_to_css, slog, LogLevel, STR_EQU)


class PrefsGui:
    """Preferences GUI handling class for Gummi."""
    
    def __init__(self, mainwindow, gummi, gui):
        """Initialize the preferences GUI.
        
        Args:
            mainwindow: The main application window
            gummi: Reference to the main Gummi instance
            gui: Reference to the main GUI instance
        """
        self.gummi = gummi
        self.gui = gui
        
        builder = Gtk.Builder()
        ui_path = os.path.join(GUMMI_DATA, "ui", "prefs.glade")
        
        # Debug information
        if not os.path.exists(ui_path):
            print(f"prefs.glade NOT FOUND at path: {ui_path}")
        else:
            print(f"Loaded prefs.glade from: {ui_path}")
        
        # Load UI file and set translation domain
        builder.add_from_file(ui_path)
        builder.set_translation_domain(C_PACKAGE)
        
        # Get widgets from builder
        self.prefwindow = builder.get_object("prefwindow")
        self.notebook = builder.get_object("notebook1")
        self.textwrap_button = builder.get_object("textwrapping")
        self.wordwrap_button = builder.get_object("wordwrapping")
        self.line_numbers = builder.get_object("line_numbers")
        self.highlighting = builder.get_object("highlighting")
        self.tabwidth = builder.get_object("tabwidth")
        self.spaces_instof_tabs = builder.get_object("spaces_instof_tabs")
        self.autoindentation = builder.get_object("autoindentation")
        self.autosaving = builder.get_object("autosaving")
        self.compile_status = builder.get_object("compile_status")
        self.autosave_timer = builder.get_object("autosave_timer")
        self.combo_languages = builder.get_object("combo_languages")
        self.styleschemes_treeview = builder.get_object("styleschemes_treeview")
        self.list_styleschemes = builder.get_object("list_styleschemes")
        self.default_text = builder.get_object("default_text")
        self.default_buffer = self.default_text.get_buffer()
        self.editor_font = builder.get_object("editor_font")
        self.compile_scheme = builder.get_object("combo_compilescheme")
        self.compile_timer = builder.get_object("compile_timer")
        self.autoexport = builder.get_object("auto_export")
        
        # Typesetter widgets
        self.typ_pdflatex = builder.get_object("typ_pdflatex")
        self.typ_xelatex = builder.get_object("typ_xelatex")
        self.typ_lualatex = builder.get_object("typ_lualatex")
        self.typ_rubber = builder.get_object("typ_rubber")
        self.typ_latexmk = builder.get_object("typ_latexmk")
        
        # Method widgets
        self.method_texpdf = builder.get_object("method_texpdf")
        self.method_texdvipdf = builder.get_object("method_texdvipdf")
        self.method_texdvipspdf = builder.get_object("method_texdvipspdf")
        
        # Options widgets
        self.opt_shellescape = builder.get_object("opt_shellescape")
        self.opt_synctex = builder.get_object("opt_synctex")
        
        # Preview widgets
        self.combo_zoom_modes = builder.get_object("combo_zoom_modes")
        self.list_zoom_modes = builder.get_object("list_zoom_modes")
        self.combo_animated_scroll = builder.get_object("combo_animated_scroll")
        self.spin_cache_size = builder.get_object("spin_cache_size")
        
        # Set transient for
        self.prefwindow.set_transient_for(mainwindow)
        
        # List available languages
        self._init_language_list()
        
        # List available style schemes
        self._init_style_schemes()
        
        # Connect signals
        self._connect_signals(builder)
    
    def _init_language_list(self):
        """Initialize the list of available spell check languages."""
        if os.path.exists(self._find_program_in_path("enchant-lsmod-2")):
            pret = utils_popen_r("enchant-lsmod-2 -list-dicts", None)
            if pret[1] is not None:
                output = pret[1].split("\n")
                for line in output:
                    if line:
                        elems = line.split(" ")
                        if elems[0]:
                            self.combo_languages.append_text(elems[0])
            
            self.combo_languages.set_active(0)
    
    def _init_style_schemes(self):
        """Initialize the list of available style schemes."""
        schemes = editor_list_style_scheme_sorted()
        
        for scheme in schemes:
            desc = f"<b>{scheme.get_name()}</b> - {scheme.get_description()}"
            iter_val = self.list_styleschemes.append()
            self.list_styleschemes.set(iter_val,
                                      0, desc,
                                      1, scheme.get_id())
    
    def _connect_signals(self, builder):
        """Connect signal handlers."""
        builder.connect_signals({
            "on_textwrapping_toggled": self.toggle_textwrapping,
            "on_wordwrapping_toggled": self.toggle_wordwrapping,
            "on_line_numbers_toggled": self.toggle_linenumbers,
            "on_highlighting_toggled": self.toggle_highlighting,
            "on_spaces_instof_tabs_toggled": self.toggle_spaces_instof_tabs,
            "on_autoindentation_toggled": self.toggle_autoindentation,
            "on_autosaving_toggled": self.toggle_autosaving,
            "on_compile_status_toggled": self.toggle_compilestatus,
            "on_auto_export_toggled": self.toggle_autoexport,
            "on_tabwidth_value_changed": self.on_tabwidth_value_changed,
            "on_autosave_timer_value_changed": self.on_autosave_value_changed,
            "on_compile_timer_value_changed": self.on_compile_value_changed,
            "on_spin_cache_size_value_changed": self.on_cache_size_value_changed,
            "on_editor_font_set": self.on_editor_font_change,
            "on_configure_snippets_clicked": self.on_configure_snippets_clicked,
            "on_typ_pdflatex_toggled": self.on_typ_pdflatex_toggled,
            "on_typ_xelatex_toggled": self.on_typ_xelatex_toggled,
            "on_typ_lualatex_toggled": self.on_typ_lualatex_toggled,
            "on_typ_rubber_toggled": self.on_typ_rubber_toggled,
            "on_typ_latexmk_toggled": self.on_typ_latexmk_toggled,
            "on_method_texpdf_toggled": self.on_method_texpdf_toggled,
            "on_method_texdvipdf_toggled": self.on_method_texdvipdf_toggled,
            "on_method_texdvipspdf_toggled": self.on_method_texdvipspdf_toggled,
            "on_opt_shellescape_toggled": self.toggle_shellescape,
            "on_opt_synctex_toggled": self.on_synctex_toggled,
            "on_combo_languages_changed": self.on_combo_language_changed,
            "on_combo_compilescheme_changed": self.on_combo_compilescheme_changed,
            "on_combo_zoom_modes_changed": self.on_combo_zoom_modes_changed,
            "on_combo_animated_scroll_changed": self.on_combo_animated_scroll_changed,
            "on_styleschemes_treeview_cursor_changed": self.on_styleschemes_treeview_cursor_changed,
            "on_prefs_close_clicked": self.on_prefs_close_clicked,
            "on_prefs_reset_clicked": self.on_prefs_reset_clicked
        })
    
    def _find_program_in_path(self, program):
        """Find a program in the system path.
        
        Args:
            program: Name of the program to find
            
        Returns:
            Full path to the program, or None if not found
        """
        for path in os.environ.get("PATH", "").split(os.pathsep):
            exec_path = os.path.join(path, program)
            if os.path.exists(exec_path) and os.access(exec_path, os.X_OK):
                return exec_path
        return None
    
    def main(self, page=0):
        """Show the preferences dialog.
        
        Args:
            page: The notebook page to show (default: 0)
        """
        self.notebook.set_current_page(page)
        
        self.set_all_tab_settings()
        self.prefwindow.show()
    
    def set_all_tab_settings(self):
        """Set all tab settings based on current configuration."""
        self.set_tab_view_settings()
        self.set_tab_editor_settings()
        self.set_tab_fontcolor_settings()
        self.set_tab_defaulttext_settings()
        self.set_tab_compilation_settings()
        self.set_tab_preview_settings()
        self.set_tab_miscellaneous_settings()
    
    def set_tab_view_settings(self):
        """Set the view tab settings."""
        textwrap_enabled = config_get_boolean("Editor", "textwrapping")
        
        if textwrap_enabled:
            self.textwrap_button.set_active(True)
            self.wordwrap_button.set_active(config_get_boolean("Editor", "wordwrapping"))
        else:
            self.wordwrap_button.set_sensitive(False)
        
        self.line_numbers.set_active(config_get_boolean("Editor", "line_numbers"))
        self.highlighting.set_active(config_get_boolean("Editor", "highlighting"))
    
    def set_tab_editor_settings(self):
        """Set the editor tab settings."""
        self.autoindentation.set_active(config_get_boolean("Editor", "autoindentation"))
        self.spaces_instof_tabs.set_active(config_get_boolean("Editor", "spaces_instof_tabs"))
        self.tabwidth.set_value(config_get_integer("Editor", "tabwidth"))
        self.autosave_timer.set_value(config_get_integer("File", "autosave_timer"))
        self.autosaving.set_active(config_get_boolean("File", "autosaving"))
        
        if not config_get_boolean("File", "autosaving"):
            self.autosave_timer.set_sensitive(False)
    
    def set_tab_fontcolor_settings(self):
        """Set the font and color tab settings."""
        self.editor_font.set_font(config_get_string("Editor", "font_str"))
        self.apply_style_scheme()
        
        # Set default font on all tabs
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.set_font(config_get_string("Editor", "font_css"))
    
    def set_tab_defaulttext_settings(self):
        """Set the default text tab settings."""
        try:
            with open(C_WELCOMETEXT, 'r') as f:
                text = f.read()
                self.default_buffer.set_text(text, -1)
                self.default_buffer.set_modified(False)
        except Exception:
            self.default_text.set_sensitive(False)
    
    def set_tab_compilation_settings(self):
        """Set the compilation tab settings."""
        # Setting available typesetters and the active one
        if pdflatex_detected():
            if pdflatex_active():
                self.typ_pdflatex.set_active(True)
            self.typ_pdflatex.set_sensitive(True)
            self.typ_pdflatex.set_tooltip_text("")
        
        if xelatex_detected():
            if xelatex_active():
                self.typ_xelatex.set_active(True)
            self.typ_xelatex.set_sensitive(True)
            self.typ_xelatex.set_tooltip_text("")
        
        if lualatex_detected():
            if lualatex_active():
                self.typ_lualatex.set_active(True)
            self.typ_lualatex.set_sensitive(True)
            self.typ_lualatex.set_tooltip_text("")
        
        if rubber_detected():
            if rubber_active():
                self.typ_rubber.set_active(True)
            self.typ_rubber.set_sensitive(True)
            self.typ_rubber.set_tooltip_text("")
        
        if latexmk_detected():
            if latexmk_active():
                self.typ_latexmk.set_active(True)
            self.typ_latexmk.set_sensitive(True)
            self.typ_latexmk.set_tooltip_text("")
        
        # Set compile method
        if latex_method_active("texpdf"):
            self.method_texpdf.set_active(True)
        elif latex_method_active("texdvipdf"):
            self.method_texdvipdf.set_active(True)
        elif latex_method_active("texdvipspdf"):
            self.method_texdvipspdf.set_active(True)
        
        # Set shell escape and synctex options
        self.opt_shellescape.set_active(latex_use_shellescaping())
        
        if latex_can_synctex():
            self.opt_synctex.set_active(config_get_boolean("Compile", "synctex"))
    
    def set_tab_preview_settings(self):
        """Set the preview tab settings."""
        pause_status = config_get_boolean("Compile", "pause")
        
        self.compile_status.set_active(not pause_status)
        
        if pause_status:
            self.compile_timer.set_sensitive(False)
        
        self.compile_timer.set_value(config_get_integer("Compile", "timer"))
        
        # Set compile scheme
        if config_value_as_str_equals("Compile", "scheme", "real_time"):
            self.compile_scheme.set_active(1)
        
        # Set default zoom mode
        conf_str = config_get_string("Preview", "zoom_mode")
        model = self.combo_zoom_modes.get_model()
        for i, row in enumerate(model):
            if STR_EQU(row[0], conf_str):
                self.combo_zoom_modes.set_active(i)
                break
        
        # Set animated scroll
        if config_value_as_str_equals("Preview", "animated_scroll", "always"):
            self.combo_animated_scroll.set_active(0)
        elif config_value_as_str_equals("Preview", "animated_scroll", "never"):
            self.combo_animated_scroll.set_active(2)
        else:
            self.combo_animated_scroll.set_active(1)
        
        self.spin_cache_size.set_value(config_get_integer("Preview", "cache_size"))
    
    def set_tab_miscellaneous_settings(self):
        """Set the miscellaneous tab settings."""
        # Set language
        lang = config_get_string("Editor", "spelling_lang")
        model = self.combo_languages.get_model()
        for i, row in enumerate(model):
            if STR_EQU(row[0], lang):
                self.combo_languages.set_active(i)
                break
        
        self.autoexport.set_active(config_get_boolean("File", "autoexport"))
    
    def apply_style_scheme(self):
        """Apply the current style scheme selection."""
        scheme_id = config_get_string("Editor", "style_scheme")
        schemes = editor_list_style_scheme_sorted()
        
        for i, scheme in enumerate(schemes):
            if STR_EQU(scheme.get_id(), scheme_id):
                path = Gtk.TreePath.new_from_string(str(i))
                self.styleschemes_treeview.set_cursor(path, None, False)
                break
        else:
            # If scheme not found, set to first one
            if schemes:
                path = Gtk.TreePath.new_from_string("0")
                self.styleschemes_treeview.set_cursor(path, None, False)
                
                # Apply "classic" scheme to all tabs
                for tab in self.gummi.tabmanager.tabs:
                    tab.editor.set_style_scheme_by_id("classic")
    
    # Signal handlers
    
    def toggle_linenumbers(self, widget):
        """Handle toggling of the line numbers option."""
        newval = widget.get_active()
        config_set_boolean("Editor", "line_numbers", newval)
        
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.view.set_show_line_numbers(newval)
    
    def toggle_highlighting(self, widget):
        """Handle toggling of the highlighting option."""
        newval = widget.get_active()
        config_set_boolean("Editor", "highlighting", newval)
        
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.view.set_highlight_current_line(newval)
    
    def toggle_textwrapping(self, widget):
        """Handle toggling of the text wrapping option."""
        newval = widget.get_active()
        config_set_boolean("Editor", "textwrapping", newval)
        
        if newval:
            for tab in self.gummi.tabmanager.tabs:
                tab.editor.view.set_wrap_mode(Gtk.WrapMode.CHAR)
            self.wordwrap_button.set_sensitive(True)
        else:
            self.wordwrap_button.set_active(False)
            for tab in self.gummi.tabmanager.tabs:
                tab.editor.view.set_wrap_mode(Gtk.WrapMode.NONE)
            self.wordwrap_button.set_sensitive(False)
    
    def toggle_wordwrapping(self, widget):
        """Handle toggling of the word wrapping option."""
        newval = widget.get_active()
        config_set_boolean("Editor", "wordwrapping", newval)
        
        for tab in self.gummi.tabmanager.tabs:
            if newval:
                tab.editor.view.set_wrap_mode(Gtk.WrapMode.WORD)
            else:
                tab.editor.view.set_wrap_mode(Gtk.WrapMode.CHAR)
    
    def toggle_compilestatus(self, widget):
        """Handle toggling of the compile status option."""
        val = widget.get_active()
        config_set_boolean("Compile", "pause", not val)
        
        self.compile_timer.set_sensitive(val)
        self.gui.previewgui.preview_pause.set_active(not val)
    
    def toggle_spaces_instof_tabs(self, widget):
        """Handle toggling of the spaces instead of tabs option."""
        newval = widget.get_active()
        config_set_boolean("Editor", "spaces_instof_tabs", newval)
        
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.view.set_insert_spaces_instead_of_tabs(newval)
    
    def toggle_autoindentation(self, widget):
        """Handle toggling of the auto indentation option."""
        newval = widget.get_active()
        config_set_boolean("Editor", "autoindentation", newval)
        
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.view.set_auto_indent(newval)
    
    def toggle_autosaving(self, widget):
        """Handle toggling of the auto saving option."""
        newval = widget.get_active()
        config_set_boolean("File", "autosaving", newval)
        
        if newval:
            self.autosave_timer.set_sensitive(True)
            self.autosave_timer.set_value(config_get_integer("File", "autosave_timer"))
            from iofunctions import iofunctions_reset_autosave
            iofunctions_reset_autosave(self.gummi.active_editor.filename)
        else:
            self.autosave_timer.set_sensitive(False)
            from iofunctions import iofunctions_stop_autosave
            iofunctions_stop_autosave()
    
    def toggle_autoexport(self, widget):
        """Handle toggling of the auto export option."""
        newval = widget.get_active()
        config_set_boolean("File", "autoexport", newval)
    
    def on_prefs_close_clicked(self, widget):
        """Handle clicking of the preferences close button."""
        if self.default_buffer.get_modified():
            start_iter = self.default_buffer.get_start_iter()
            end_iter = self.default_buffer.get_end_iter()
            text = self.default_buffer.get_text(start_iter, end_iter, False)
            utils_set_file_contents(C_WELCOMETEXT, text, -1)
        
        self.prefwindow.hide()
        config_save()
    
    def on_prefs_reset_clicked(self, widget):
        """Handle clicking of the preferences reset button."""
        config_load_defaults()
        utils_copy_file(C_DEFAULTTEXT, C_WELCOMETEXT, None)
        
        self.set_all_tab_settings()
    
    def on_tabwidth_value_changed(self, widget):
        """Handle change of the tab width value."""
        newval = int(widget.get_value())
        config_set_integer("Editor", "tabwidth", newval)
        
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.view.set_tab_width(newval)
    
    def on_configure_snippets_clicked(self, widget):
        """Handle clicking of the configure snippets button."""
        from gui.gui_snippets import snippetsgui_main
        snippetsgui_main(self.gui.snippetsgui)
    
    def on_autosave_value_changed(self, widget):
        """Handle change of the autosave timer value."""
        newval = int(widget.get_value())
        config_set_integer("File", "autosave_timer", newval)
        
        from iofunctions import iofunctions_reset_autosave
        iofunctions_reset_autosave(self.gummi.active_editor.filename)
    
    def on_compile_value_changed(self, widget):
        """Handle change of the compile timer value."""
        newval = int(widget.get_value())
        config_set_integer("Compile", "timer", newval)
        
        self.gui.previewgui.reset()
    
    def on_cache_size_value_changed(self, widget):
        """Handle change of the cache size value."""
        newval = int(widget.get_value())
        config_set_integer("Preview", "cache_size", newval)
        
        GLib.idle_add(self.run_garbage_collector, self.gui.previewgui)
    
    def on_editor_font_change(self, widget):
        """Handle change of the editor font."""
        font_str = widget.get_font()
        config_set_string("Editor", "font_str", font_str)
        slog(LogLevel.INFO, f"setting font to {font_str}")
        
        font_desc = widget.get_font_desc()
        font_css = utils_pango_font_desc_to_css(font_desc)
        config_set_string("Editor", "font_css", font_css)
        
        # Set new font on all tabs
        for tab in self.gummi.tabmanager.tabs:
            tab.editor.set_font(font_css)
    
    def on_typ_pdflatex_toggled(self, widget):
        """Handle toggling of the pdflatex typesetter option."""
        if widget.get_active():
            config_set_string("Compile", "typesetter", "pdflatex")
            typesetter_setup()
    
    def on_typ_xelatex_toggled(self, widget):
        """Handle toggling of the xelatex typesetter option."""
        if widget.get_active():
            config_set_string("Compile", "typesetter", "xelatex")
            typesetter_setup()
    
    def on_typ_lualatex_toggled(self, widget):
        """Handle toggling of the lualatex typesetter option."""
        if widget.get_active():
            config_set_string("Compile", "typesetter", "lualatex")
            typesetter_setup()
    
    def on_typ_rubber_toggled(self, widget):
        """Handle toggling of the rubber typesetter option."""
        if widget.get_active():
            config_set_string("Compile", "typesetter", "rubber")
            typesetter_setup()
    
    def on_typ_latexmk_toggled(self, widget):
        """Handle toggling of the latexmk typesetter option."""
        if widget.get_active():
            config_set_string("Compile", "typesetter", "latexmk")
            typesetter_setup()
    
    def on_method_texpdf_toggled(self, widget):
        """Handle toggling of the tex->pdf compile method."""
        if widget.get_active():
            config_set_string("Compile", "steps", "texpdf")
            slog(LogLevel.INFO, "Changed compile method to \"tex->pdf\"")
    
    def on_method_texdvipdf_toggled(self, widget):
        """Handle toggling of the tex->dvi->pdf compile method."""
        if widget.get_active():
            config_set_string("Compile", "steps", "texdvipdf")
            slog(LogLevel.INFO, "Changed compile method to \"tex->dvi->pdf\"")
    
    def on_method_texdvipspdf_toggled(self, widget):
        """Handle toggling of the tex->dvi->ps->pdf compile method."""
        if widget.get_active():
            config_set_string("Compile", "steps", "texdvipspdf")
            slog(LogLevel.INFO, "Changed compile method to \"tex->dvi->ps->pdf\"")
    
    def toggle_shellescape(self, widget):
        """Handle toggling of the shell escape option."""
        newval = widget.get_active()
        config_set_boolean("Compile", "shellescape", newval)
    
    def on_synctex_toggled(self, widget):
        """Handle toggling of the synctex option."""
        newval = widget.get_active()
        config_set_boolean("Compile", "synctex", newval)
        self.gui.menu_autosync.set_sensitive(newval)
    
    def on_combo_language_changed(self, widget):
        """Handle change of the spellcheck language."""
        selected = widget.get_active_text()
        config_set_string("Editor", "spelling_lang", selected)
        
        if config_get_boolean("Editor", "spelling"):
            for tab in self.gummi.tabmanager.tabs:
                tab.editor.activate_spellchecking(False)
                tab.editor.activate_spellchecking(True)
    
    def on_combo_compilescheme_changed(self, widget):
        """Handle change of the compile scheme."""
        selected = widget.get_active()
        scheme = ["on_idle", "real_time"][selected]
        slog(LogLevel.INFO, f"compile scheme set to {scheme}")
        config_set_string("Compile", "scheme", scheme)
        self.gui.previewgui.reset()
    
    def on_combo_zoom_modes_changed(self, widget):
        """Handle change of the zoom mode."""
        selected = widget.get_active()
        # Lazily duplicating this again for now (TODO fix in 0.8.1)
        scheme = ["Best Fit", "Fit Page Width", "50%", "70%", "85%",
                 "100%", "125%", "150%", "200%", "300%", "400%"]
        config_set_string("Preview", "zoom_mode", scheme[selected])
    
    def on_combo_animated_scroll_changed(self, widget):
        """Handle change of the animated scroll option."""
        selected = widget.get_active()
        scheme = ["always", "autosync", "never"]
        config_set_string("Preview", "animated_scroll", scheme[selected])
    
    def on_styleschemes_treeview_cursor_changed(self, treeview):
        """Handle change of the style scheme selection."""
        selection = treeview.get_selection()
        model, iter_val = selection.get_selected()
        
        if iter_val:
            name = model.get_value(iter_val, 0)
            style_id = model.get_value(iter_val, 1)
            
            for tab in self.gummi.tabmanager.tabs:
                tab.editor.set_style_scheme_by_id(style_id)
            
            config_set_string("Editor", "style_scheme", style_id)
    
    def run_garbage_collector(self, previewgui):
        """Run the garbage collector for the preview."""
        # This would be implemented in previewgui
        # Just calling the appropriate method
        return False  # Return False to remove from idle queue


def prefsgui_init(mainwindow, gummi, gui):
    """Initialize and return a new PrefsGui instance.
    
    Args:
        mainwindow: The main application window
        gummi: Reference to the main Gummi instance
        gui: Reference to the main GUI instance
        
    Returns:
        A new PrefsGui instance
    """
    return PrefsGui(mainwindow, gummi, gui)


def prefsgui_main(prefs, page=0):
    """Show the preferences dialog (wrapper function).
    
    Args:
        prefs: The PrefsGui instance
        page: The notebook page to show (default: 0)
    """
    prefs.main(page)


def prefsgui_apply_style_scheme(prefs):
    """Apply the current style scheme selection (wrapper function).
    
    Args:
        prefs: The PrefsGui instance
    """
    prefs.apply_style_scheme()


# Helper functions that would be defined elsewhere
def editor_list_style_scheme_sorted():
    """List all available style schemes, sorted by name.
    
    Returns:
        Sorted list of GtkSourceStyleScheme objects
    """
    # This would be implemented in the editor module
    # For now, return a dummy list
    manager = GtkSource.StyleSchemeManager.get_default()
    scheme_ids = manager.get_scheme_ids()
    schemes = [manager.get_scheme(scheme_id) for scheme_id in scheme_ids]
    return sorted(schemes, key=lambda s: s.get_name().lower())