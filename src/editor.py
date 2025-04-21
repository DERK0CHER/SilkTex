#!/usr/bin/env python3
"""
editor.py - Editor component for Gummi

Copyright (C) 2025 Gummi Developers
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
import re
import stat
import shutil
import tempfile
from pathlib import Path
import math
import time

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
gi.require_version('Gspell', '1')
from gi.repository import Gtk, GtkSource, Gspell, Gdk, GLib, Gio

from configfile import config_get_string, config_get_integer, config_get_boolean
from constants import C_TMPDIR, C_GUMMI_CONFDIR, DIR_PERMS
from environment import slog, L_INFO, L_WARNING, L_ERROR
import utils

# Text styling constants
STYLE = [
    ["tool_bold", "\\textbf{", "}"],
    ["tool_italic", "\\emph{", "}"],
    ["tool_unline", "\\underline{", "}"],
    ["tool_left", "\\begin{flushleft}", "\\end{flushleft}"],
    ["tool_center", "\\begin{center}", "\\end{center}"],
    ["tool_right", "\\begin{flushright}", "\\end{flushright}"]
]


class GuEditor:
    """Editor component for Gummi"""

    def __init__(self, motion_controller):
        """Initialize the editor component
        
        Args:
            motion_controller: GuMotion controller for motion events
        """
        # File related member initialization
        self.workfd = None
        self.fdname = None
        self.filename = None  # current opened file name in workspace
        self.basename = None  # used to form .dvi/.ps/.log etc. files
        self.pdffile = None
        self.workfile = None
        self.bibfile = None
        self.projfile = None
        self.last_modtime = 0
        self.last_edit = None
        self.sync_to_last_edit = False

        # Create the text buffer and view
        language_manager = GtkSource.LanguageManager.get_default()
        lang = language_manager.get_language("latex")
        self.buffer = GtkSource.Buffer.new_with_language(lang)
        self.view = GtkSource.View.new_with_buffer(self.buffer)
        self.stylemanager = GtkSource.StyleSchemeManager.get_default()
        
        # Create tags for error and search highlighting
        self.errortag = Gtk.TextTag.new("error")
        self.searchtag = Gtk.TextTag.new("search")
        self.editortags = self.buffer.get_tag_table()
        
        # Setup search and replace properties
        self.replace_activated = False
        self.term = None
        self.backwards = False
        self.wholeword = False
        self.matchcase = False

        # Create CSS provider for styling
        self.css = Gtk.CssProvider.new()
        context = self.view.get_style_context()
        context.add_provider(self.css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        # Configure the source view
        self.view.set_tab_width(config_get_integer("Editor", "tabwidth"))
        self.view.set_insert_spaces_instead_of_tabs(config_get_boolean("Editor", "spaces_instof_tabs"))
        self.view.set_auto_indent(config_get_boolean("Editor", "autoindentation"))

        # Enable spell checking if configured
        if config_get_boolean("Editor", "spelling"):
            self.activate_spellchecking(True)

        # Configure the source view further
        self.sourceview_config()
        self.buffer.set_modified(False)

        # Connect signals
        self.sigid = [
            self.view.connect("key-press-event", self.on_key_press_cb, motion_controller),
            self.view.connect("key-release-event", self.on_key_release_cb, motion_controller),
            self.buffer.connect("changed", self.check_preview_timer),
            self.buffer.connect_after("insert-text", self.on_inserted_text),
            self.buffer.connect_after("delete-range", self.on_delete_range)
        ]

    def on_key_press_cb(self, widget, event, motion_controller):
        """Callback for key press events
        
        Args:
            widget: The widget that received the event
            event: The key event
            motion_controller: The motion controller
            
        Returns:
            bool: True if the event was handled
        """
        # This would be implemented as part of the motion controller
        pass
        
    def on_key_release_cb(self, widget, event, motion_controller):
        """Callback for key release events
        
        Args:
            widget: The widget that received the event
            event: The key event
            motion_controller: The motion controller
            
        Returns:
            bool: True if the event was handled
        """
        # This would be implemented as part of the motion controller
        pass
        
    def check_preview_timer(self, *args):
        """Timer callback to check if preview should be updated"""
        # This would be implemented as part of the preview system
        pass

    def on_inserted_text(self, textbuffer, location, text, length):
        """Callback for text insertion
        
        Args:
            textbuffer: The text buffer
            location: Text iter at insertion point
            text: The inserted text
            length: Length of inserted text
        """
        if location is None:
            return
        
        self.last_edit = location.copy()
        self.sync_to_last_edit = True

    def on_delete_range(self, textbuffer, start, end):
        """Callback for text deletion
        
        Args:
            textbuffer: The text buffer
            start: Start text iter
            end: End text iter
        """
        if start is None:
            return
            
        self.last_edit = start.copy()
        self.sync_to_last_edit = True

    def destroy(self):
        """Destroy the editor component"""
        # Disconnect signals
        for i in range(2):
            if self.view.handler_is_connected(self.sigid[i]):
                self.view.disconnect(self.sigid[i])
                
        for i in range(2, 5):
            if self.buffer.handler_is_connected(self.sigid[i]):
                self.buffer.disconnect(self.sigid[i])
                
        # Clean up file info
        self.fileinfo_cleanup()

    def fileinfo_update(self, filename=None):
        """Update file information
        
        When a TeX document includes materials from other files (image, documents,
        bibliography ... etc), pdflatex will try to find those files under the
        working directory if include path is not absolute.
        
        Args:
            filename: The filename to update
        """
        # Check if cache directory exists
        if not os.path.isdir(C_TMPDIR):
            slog(L_WARNING, "gummi cache directory does not exist, creating..")
            os.makedirs(C_TMPDIR, mode=DIR_PERMS, exist_ok=True)
            
        # Clean up existing work files
        if self.workfd is not None:
            self.fileinfo_cleanup()
            
        # Create temporary file
        fd, self.fdname = tempfile.mkstemp(dir=C_TMPDIR, prefix="gummi_")
        self.workfd = fd
        
        if filename:
            # Get absolute path if needed
            if not os.path.isabs(filename):
                fname = os.path.join(os.getcwd(), filename)
            else:
                fname = filename
                
            base = os.path.basename(fname)
            dirname = os.path.dirname(fname)
            
            self.filename = fname
            self.basename = os.path.join(dirname, f".{base}")
            self.workfile = f"{self.basename}.swp"
            self.pdffile = os.path.join(C_TMPDIR, f".{base}.pdf")
            
            # Get last modified time
            if os.path.exists(fname):
                self.last_modtime = os.stat(fname).st_mtime
        else:
            self.workfile = self.fdname
            self.basename = self.fdname
            self.pdffile = f"{self.fdname}.pdf"

    def fileinfo_update_biblio(self, filename):
        """Update bibliography file information
        
        Args:
            filename: The bibliography filename
            
        Returns:
            bool: True if the bibliography file exists
        """
        if self.bibfile:
            del self.bibfile
            
        if self.filename and not os.path.isabs(filename):
            dirname = os.path.dirname(self.filename)
            self.bibfile = os.path.join(dirname, filename)
        else:
            self.bibfile = filename
            
        return os.path.exists(self.bibfile)

    def fileinfo_cleanup(self):
        """Clean up temporary files"""
        # Define file paths to clean
        auxfile = logfile = syncfile = None
        
        if self.filename:
            base = os.path.basename(self.filename)
            auxfile = os.path.join(C_TMPDIR, f".{base}.aux")
            logfile = os.path.join(C_TMPDIR, f".{base}.log")
            syncfile = os.path.join(C_TMPDIR, f".{base}.synctex.gz")
        else:
            auxfile = f"{self.fdname}.aux"
            logfile = f"{self.fdname}.log"
            syncfile = f"{self.fdname}.synctex.gz"
            
        # Close the work file descriptor
        if self.workfd:
            os.close(self.workfd)
            self.workfd = None
            
        # Remove temporary files
        for file in [auxfile, logfile, syncfile, self.fdname, 
                    self.workfile, self.pdffile, self.basename]:
            if file and os.path.exists(file):
                try:
                    os.remove(file)
                except OSError:
                    pass
                    
        # Reset file attributes
        self.fdname = None
        self.filename = None
        self.workfile = None
        self.pdffile = None
        self.basename = None

    def sourceview_config(self):
        """Configure the source view"""
        self.buffer.set_highlight_matching_brackets(True)
        
        # Set style scheme
        style_scheme = config_get_string("Editor", "style_scheme")
        self.set_style_scheme_by_id(style_scheme)
        
        # Set font
        self.set_font(config_get_string("Editor", "font_css"))
        
        # Configure view options
        self.view.set_show_line_numbers(config_get_boolean("Editor", "line_numbers"))
        self.view.set_highlight_current_line(config_get_boolean("Editor", "highlighting"))
        
        # Set text wrapping mode
        wrapmode = 0
        if config_get_boolean("Editor", "textwrapping"):
            wrapmode += 1
        if config_get_boolean("Editor", "wordwrapping"):
            wrapmode += 1
            
        self.view.set_wrap_mode(wrapmode)

    def activate_spellchecking(self, status):
        """Activate or deactivate spell checking
        
        Args:
            status: True to activate, False to deactivate
        """
        lang = config_get_string("Editor", "spelling_lang")
        
        if status:
            # Create spell checker
            checker = Gspell.Checker.new(Gspell.Language.lookup(lang))
            buffer_adapter = Gspell.TextBufferAdapter.get_from_gtk_text_buffer(self.buffer)
            buffer_adapter.set_spell_checker(checker)
            
            # Attach spell checker to view
            view_adapter = Gspell.TextView.get_from_gtk_text_view(self.view)
            view_adapter.set_inline_spell_checking(True)
            view_adapter.set_enable_language_menu(True)
        else:
            # Remove spell checker
            view_adapter = Gspell.TextView.get_from_gtk_text_view(self.view)
            if view_adapter:
                view_adapter.set_inline_spell_checking(False)

    def fill_buffer(self, text):
        """Fill the buffer with text
        
        Args:
            text: The text to fill the buffer with
        """
        self.buffer.begin_user_action()
        self.buffer.begin_not_undoable_action()
        self.view.set_sensitive(False)
        self.buffer.set_text(text)
        self.view.set_sensitive(True)
        self.buffer.end_not_undoable_action()
        self.buffer.end_user_action()
        
        # Place cursor at the beginning
        start = self.buffer.get_start_iter()
        self.buffer.place_cursor(start)
        self.view.grab_focus()
        self.sync_to_last_edit = False

    def grab_buffer(self):
        """Get the buffer text
        
        Returns:
            str: The buffer text
        """
        start = self.buffer.get_start_iter()
        end = self.buffer.get_end_iter()
        return self.buffer.get_text(start, end, False)

    def buffer_changed(self):
        """Check if the buffer has been modified
        
        Returns:
            bool: True if modified, False otherwise
        """
        return self.buffer.get_modified()

    def insert_package(self, package, options=None):
        """Insert a package into the document
        
        Args:
            package: The package name
            options: Optional package options
        """
        if options:
            pkgstr = f"\\usepackage[{options}]{{{package}}}\n"
        else:
            pkgstr = f"\\usepackage{{{package}}}\n"
            
        start = self.buffer.get_start_iter()
        
        # Find document beginning
        search = start.forward_search("\\begin{document}", 0, None)
        if not search:
            return
            
        mstart, mend = search
        
        # Check if package is already included
        if not start.backward_search(pkgstr, 0, None):
            self.buffer.begin_not_undoable_action()
            self.buffer.begin_user_action()
            self.buffer.insert(mstart, pkgstr)
            self.buffer.end_user_action()
            self.buffer.end_not_undoable_action()
            self.buffer.set_modified(True)

    def insert_bib(self, package):
        """Insert bibliography commands
        
        Args:
            package: The bibliography package name
        """
        pkgstr = f"\\bibliography{{{package}}}{{}}\n\\bibliographystyle{{plain}}\n"
        
        start = self.buffer.get_start_iter()
        end = self.buffer.get_end_iter()
        
        # Find document end
        search = end.backward_search("\\end{document}", 0, None)
        if not search:
            return
            
        mstart, mend = search
        
        # Check if bibliography is already included
        if not start.forward_search("\\bibliography{", 0, None):
            self.buffer.begin_not_undoable_action()
            self.buffer.begin_user_action()
            self.buffer.insert(mstart, pkgstr)
            self.buffer.end_user_action()
            self.buffer.end_not_undoable_action()
            self.buffer.set_modified(True)

    def set_selection_textstyle(self, style_type):
        """Apply LaTeX styling to selected text
        
        Args:
            style_type: The style type to apply
        """
        # Get selection bounds
        bounds = self.buffer.get_selection_bounds()
        if not bounds:
            return
            
        start, end = bounds
        selected_text = self.buffer.get_text(start, end, False)
        
        # Find the style to apply
        selected = None
        for i, style_entry in enumerate(STYLE):
            if style_entry[0] == style_type:
                selected = i
                break
                
        if selected is None:
            return
            
        # Prepare regex for matching existing style
        style_start = STYLE[selected][1]
        style_end = STYLE[selected][2]
        
        # Escape regex special characters if needed
        if style_start.startswith('\\'):
            style_start = '\\' + style_start
        if style_end.startswith('\\'):
            style_end = '\\' + style_end
            
        pattern = f"(.*){style_start}(.*){style_end}(.*)"
        regex = re.compile(pattern, re.DOTALL)
        match = regex.match(selected_text)
        
        if match:
            # Style is already applied
            groups = match.groups()
            if not groups[0] and not groups[2]:
                # Style completely encompasses the selection, so remove it
                outtext = groups[1]
            else:
                # Partial match, apply to entire selection
                outtext = f"{STYLE[selected][1]}{groups[0]}{groups[1]}{groups[2]}{STYLE[selected][2]}"
        else:
            # No previous style applied
            outtext = f"{STYLE[selected][1]}{selected_text}{STYLE[selected][2]}"
            
        # Apply the changes
        self.buffer.begin_user_action()
        self.buffer.delete(start, end)
        self.buffer.insert(start, outtext)
        
        # Reselect the text
        new_end = start.copy()
        new_end.forward_chars(len(outtext))
        self.buffer.select_range(start, new_end)
        self.buffer.end_user_action()
        self.buffer.set_modified(True)

    def apply_errortags(self, lines):
        """Apply error tags to lines with errors
        
        Args:
            lines: List of line numbers with errors
        """
        # Remove existing error tags
        if self.editortags.lookup("error"):
            self.editortags.remove(self.errortag)
            
        self.editortags.add(self.errortag)
        
        # Apply tags to each line with an error
        for line_num in lines:
            if line_num == 0:  # 0 is end of list marker in C version
                break
                
            start = self.buffer.get_iter_at_line(line_num - 1)
            end = self.buffer.get_iter_at_line(line_num)
            self.buffer.apply_tag(self.errortag, start, end)

    def jumpto_search_result(self, direction):
        """Jump to next/previous search result
        
        Args:
            direction: 1 for next, -1 for previous
        """
        if not self.term:
            return
            
        if direction == 1:
            self.search_next(False)
        else:
            self.search_next(True)

    def start_search(self, term, backwards=False, wholeword=False, matchcase=False):
        """Start a search operation
        
        Args:
            term: The search term
            backwards: Search backwards
            wholeword: Match whole words only
            matchcase: Match case
        """
        # Save options
        if self.term != term:
            self.term = term
            
        self.backwards = backwards
        self.wholeword = wholeword
        self.matchcase = matchcase
        
        self.apply_searchtag()
        self.search_next(False)

    def apply_searchtag(self):
        """Apply search tag to all matches"""
        start = self.buffer.get_start_iter()
        
        # Remove existing search tags
        if self.editortags.lookup("search"):
            self.editortags.remove(self.searchtag)
            
        self.editortags.add(self.searchtag)
        
        # Set search flags
        flags = 0 if self.matchcase else Gtk.TextSearchFlags.CASE_INSENSITIVE
        
        # Find and tag all matches
        while True:
            match = None
            
            # Find next match
            match = start.forward_search(self.term, flags, None)
            if not match:
                break
                
            mstart, mend = match
            start = mend
            
            # Check if whole word match is required
            if self.wholeword and not (mstart.starts_word() and mend.ends_word()):
                continue
                
            # Apply tag to match
            self.buffer.apply_tag(self.searchtag, mstart, mend)

    def search_next(self, inverse=False):
        """Find next search match
        
        Args:
            inverse: Invert search direction
        """
        # Get current cursor position
        mark = self.buffer.get_insert()
        current = self.buffer.get_iter_at_mark(mark)
        
        # Set search flags
        flags = 0 if self.matchcase else Gtk.TextSearchFlags.CASE_INSENSITIVE
        
        # Determine search direction
        if self.backwards ^ inverse:
            # Search backwards
            match = current.backward_search(self.term, flags, None)
        else:
            # Search forwards
            current.forward_chars(len(self.term))
            match = current.forward_search(self.term, flags, None)
            
        # Process match
        if match:
            mstart, mend = match
            
            # Check if whole word match is required
            if self.wholeword and not (mstart.starts_word() and mend.ends_word()):
                # Continue searching
                if self.backwards ^ inverse:
                    current = mstart
                else:
                    current = mend
                return self.search_next(inverse)
                
            # Select and scroll to match
            self.buffer.select_range(mstart, mend)
            self.scroll_to_cursor()
        else:
            # Check if we reached the start/end of document
            if self.backwards ^ inverse:
                response = utils.yes_no_dialog(_("Top reached, search from bottom?"))
                if response == Gtk.ResponseType.YES:
                    self.buffer.place_cursor(self.buffer.get_end_iter())
                    self.search_next(inverse)
            else:
                response = utils.yes_no_dialog(_("Bottom reached, search from top?"))
                if response == Gtk.ResponseType.YES:
                    self.buffer.place_cursor(self.buffer.get_start_iter())
                    self.search_next(inverse)

    def start_replace_next(self, term, rterm, backwards=False, 
                          wholeword=False, matchcase=False):
        """Replace next occurrence and move to next match
        
        Args:
            term: Search term
            rterm: Replacement term
            backwards: Search backwards
            wholeword: Match whole words only
            matchcase: Match case
        """
        if not self.replace_activated:
            self.replace_activated = True
            self.start_search(term, backwards, wholeword, matchcase)
            return
            
        # Get current cursor position
        mark = self.buffer.get_insert()
        current = self.buffer.get_iter_at_mark(mark)
        
        # Set search flags
        flags = 0 if matchcase else Gtk.TextSearchFlags.CASE_INSENSITIVE
        
        # Find current match
        if backwards:
            match = current.backward_search(term, flags, None)
        else:
            match = current.forward_search(term, flags, None)
            
        if match:
            mstart, mend = match
            
            # Check if whole word match is required
            if wholeword and not (mstart.starts_word() and mend.ends_word()):
                return
                
            # Replace the text
            self.buffer.begin_user_action()
            self.buffer.delete(mstart, mend)
            self.buffer.insert(mstart, rterm)
            self.buffer.end_user_action()
            
            # Continue to next match
            self.search_next(False)

    def start_replace_all(self, term, rterm, backwards=False, 
                         wholeword=False, matchcase=False):
        """Replace all occurrences at once
        
        Args:
            term: Search term
            rterm: Replacement term
            backwards: Search backwards (not used but kept for API compatibility)
            wholeword: Match whole words only
            matchcase: Match case
        """
        start = self.buffer.get_start_iter()
        action_started = False
        
        # Set search flags
        flags = 0 if matchcase else Gtk.TextSearchFlags.CASE_INSENSITIVE
        
        while True:
            # Find next match
            match = start.forward_search(term, flags, None)
            if not match:
                break
                
            mstart, mend = match
            
            # Check if whole word match is required
            if wholeword and not (mstart.starts_word() and mend.ends_word()):
                start = mend
                continue
                
            # Replace the match
            if not action_started:
                self.buffer.begin_user_action()
                action_started = True
                
            self.buffer.delete(mstart, mend)
            self.buffer.insert(mstart, rterm)
            start = mstart
            
        # End the user action if any replacements were made
        if action_started:
            self.buffer.end_user_action()

    def get_current_iter(self):
        """Get iterator at current cursor position
        
        Returns:
            GtkTextIter: Iterator at cursor position
        """
        mark = self.buffer.get_insert()
        return self.buffer.get_iter_at_mark(mark)

    def scroll_to_cursor(self):
        """Scroll view to show the cursor position"""
        self.view.scroll_mark_onscreen(self.buffer.get_insert())

    def scroll_to_line(self, line):
        """Scroll to a specific line number
        
        Args:
            line: Line number to scroll to
        """
        if self is None:
            return
            
        text_iter = self.buffer.get_iter_at_line(line)
        self.buffer.place_cursor(text_iter)
        self.scroll_to_cursor()
        self.sync_to_last_edit = False

    def undo_change(self):
        """Undo the last edit"""
        if self.buffer.can_undo():
            self.buffer.undo()
            self.scroll_to_cursor()
            self.buffer.set_modified(True)

    def redo_change(self):
        """Redo the last undone edit"""
        if self.buffer.can_redo():
            self.buffer.redo()
            self.scroll_to_cursor()
            self.buffer.set_modified(True)

    def set_font(self, font_css):
        """Set editor font from CSS
        
        Args:
            font_css: CSS font definition
        """
        self.css.load_from_data(font_css.encode('utf-8'))

    def set_style_scheme_by_id(self, scheme_id):
        """Set the editor color scheme
        
        Args:
            scheme_id: ID of the style scheme
        """
        scheme = self.stylemanager.get_scheme(scheme_id)
        slog(L_INFO, f"Setting styles scheme to {scheme_id}")
        
        if not scheme:
            slog(L_ERROR, f"No style scheme {scheme_id} found, setting to classic")
            scheme = self.stylemanager.get_scheme("classic")
            
        self.buffer.set_style_scheme(scheme)
        
        # Set tag colors
        self.set_style_fg_bg(self.searchtag, scheme, "search-match", "yellow")
        self.set_style_fg_bg(self.errortag, scheme, "def:error", "red")

    def set_style_fg_bg(self, obj, scheme, style_name, default_bg):
        """Set foreground and background colors for a tag
        
        Args:
            obj: The object to style
            scheme: The style scheme
            style_name: Name of the style
            default_bg: Default background color
        """
        if scheme is None:
            self._set_default_colors(obj, default_bg)
            return
            
        style = scheme.get_style(style_name)
        if style is None:
            self._set_default_colors(obj, default_bg)
            return
            
        foreground_set = style.props.foreground_set
        foreground = style.props.foreground
        background_set = style.props.background_set
        background = style.props.background
        
        if foreground_set and background_set:
            # We trust the style to set both to good values
            obj.props.foreground = foreground
            obj.props.background = background
            
        elif not background_set and foreground_set:
            # Set bg to default and check if fg has enough contrast
            bg_rgba = Gdk.RGBA()
            bg_rgba.parse(default_bg)
            fg_rgba = Gdk.RGBA()
            fg_rgba.parse(foreground)
            
            diff = abs(self._rgba_luminance(fg_rgba) - self._rgba_luminance(bg_rgba))
            
            if diff < 0.5:
                slog(L_INFO, f"Style \"{style_name}\" defines a foreground, but no background "
                             "color. As the foreground color has not enough "
                             "contrast to Gummi's default background color, the "
                             "foreground color has been adjusted.")
                if self._rgba_luminance(bg_rgba) > 0.5:
                    fg_rgba.parse("black")
                else:
                    fg_rgba.parse("white")
                    
            obj.props.foreground = fg_rgba.to_string()
            obj.props.background = bg_rgba.to_string()
            
        elif background_set and not foreground_set:
            # Choose fg = white or black, which has more contrast
            bg_rgba = Gdk.RGBA()
            bg_rgba.parse(background)
            
            fg_color = "black" if self._rgba_luminance(bg_rgba) > 0.5 else "white"
            fg_rgba = Gdk.RGBA()
            fg_rgba.parse(fg_color)
            
            obj.props.foreground = fg_rgba.to_string()
            obj.props.background = bg_rgba.to_string()
            
        else:
            # None set, set defaults