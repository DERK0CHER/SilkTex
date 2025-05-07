#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - LaTeX Editor component

This module implements the syntax-highlighting editor component
for LaTeX editing with GtkSourceView.
"""

import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')

from gi.repository import Gtk, GLib, Gio, GObject, Gdk, GtkSource


class LatexEditor(GtkSource.View):
    """LaTeX editor component with syntax highlighting and editing features"""
    
    __gsignals__ = {
        'changed': (GObject.SignalFlags.RUN_FIRST, None, ()),
        'cursor-moved': (GObject.SignalFlags.RUN_FIRST, None, (int, int))
    }
    
    def __init__(self):
        """Initialize the LaTeX editor component."""
        super().__init__()
        
        # Set up buffer with LaTeX language
        self.buffer = GtkSource.Buffer()
        self.set_buffer(self.buffer)
        
        # Set up language manager and get LaTeX language
        lang_manager = GtkSource.LanguageManager.get_default()
        latex_lang = lang_manager.get_language("latex")
        if latex_lang:
            self.buffer.set_language(latex_lang)
        
        # Set up style scheme manager and apply a style
        style_manager = GtkSource.StyleSchemeManager.get_default()
        style = style_manager.get_scheme("classic")
        if style:
            self.buffer.set_style_scheme(style)
        
        # Configure editor view
        self.set_show_line_numbers(True)
        self.set_tab_width(4)
        self.set_auto_indent(True)
        self.set_insert_spaces_instead_of_tabs(True)
        self.set_highlight_current_line(True)
        self.set_smart_backspace(True)
        
        # Set monospace font
        self.set_monospace(True)
        
        # Add right margin
        self.set_right_margin_position(80)
        self.set_show_right_margin(True)
        
        # Set up tracking for modifications
        self.modified = False
        self.buffer.connect("changed", self.on_buffer_changed)
        self.buffer.connect("mark-set", self.on_mark_set)
        
        # Search and replace context
        self.search_context = None
        self.replace_activated = False
        
        # Enable auto-completion
        self.setup_completion()
    
    def setup_completion(self):
        """Set up the auto-completion feature"""
        completion = self.get_completion()
        provider = LaTeXCompletionProvider()
        completion.add_provider(provider)
        completion.set_property("remember-info-visibility", True)
        completion.set_property("show-icons", True)
        
    def on_buffer_changed(self, buffer):
        """Handle buffer content changes"""
        self.modified = True
        self.emit("changed")
    
    def on_mark_set(self, buffer, location, mark):
        """Handle cursor movements"""
        if mark == buffer.get_insert():
            iter = buffer.get_iter_at_mark(mark)
            line = iter.get_line() + 1  # Make 1-based for display
            column = iter.get_line_offset() + 1  # Make 1-based for display
            self.emit("cursor-moved", line, column)
    
    def get_text(self):
        """Get the full text content from the buffer
        
        Returns:
            str: The full text content
        """
        start = self.buffer.get_start_iter()
        end = self.buffer.get_end_iter()
        return self.buffer.get_text(start, end, True)
    
    def set_text(self, text):
        """Set the buffer text content
        
        Args:
            text: The text content to set
        """
        self.buffer.set_text(text)
        self.modified = False
    
    def insert_text(self, text):
        """Insert text at the current cursor position
        
        Args:
            text: The text to insert
        """
        self.buffer.insert_at_cursor(text)
    
    def start_search(self, search_text, backwards=False, whole_word=False, match_case=False):
        """Start a search operation
        
        Args:
            search_text: The text to search for
            backwards: Whether to search backwards
            whole_word: Whether to match whole words only
            match_case: Whether to match case
            
        Returns:
            bool: True if text was found, False otherwise
        """
        if not search_text:
            return False
        
        # Create search settings
        settings = GtkSource.SearchSettings()
        settings.set_search_text(search_text)
        settings.set_case_sensitive(match_case)
        settings.set_at_word_boundaries(whole_word)
        settings.set_wrap_around(True)
        
        # Create search context
        self.search_context = GtkSource.SearchContext.new(self.buffer, settings)
        
        # Get current cursor position
        cursor_mark = self.buffer.get_insert()
        cursor_iter = self.buffer.get_iter_at_mark(cursor_mark)
        
        # Search for the text
        if backwards:
            found, start_iter, end_iter = self.search_context.backward(cursor_iter)
        else:
            found, start_iter, end_iter = self.search_context.forward(cursor_iter)
        
        if found:
            # Select the found text
            self.buffer.select_range(start_iter, end_iter)
            
            # Scroll to the selection
            self.scroll_to_mark(self.buffer.get_insert(), 0.25, False, 0.5, 0.5)
            
            # Activate replace mode
            self.replace_activated = True
            
            return True
        else:
            # No match found
            self.replace_activated = False
            
            # Show a message (this would typically be handled by the window class)
            print("Text not found:", search_text)
            
            return False
    
    def start_replace_next(self, search_text, replace_text, backwards=False, whole_word=False, match_case=False):
        """Replace the next occurrence of search_text with replace_text
        
        Args:
            search_text: The text to search for
            replace_text: The replacement text
            backwards: Whether to search backwards
            whole_word: Whether to match whole words only
            match_case: Whether to match case
            
        Returns:
            bool: True if text was replaced, False otherwise
        """
        if not self.replace_activated:
            # Start a new search to find the first occurrence
            if not self.start_search(search_text, backwards, whole_word, match_case):
                return False
        
        # Get the current selection
        bounds = self.buffer.get_selection_bounds()
        if not bounds:
            return False
        
        start_iter, end_iter = bounds
        
        # Replace the selected text
        self.buffer.begin_user_action()
        self.buffer.delete(start_iter, end_iter)
        self.buffer.insert(start_iter, replace_text)
        self.buffer.end_user_action()
        
        # Find the next occurrence
        cursor_mark = self.buffer.get_insert()
        cursor_iter = self.buffer.get_iter_at_mark(cursor_mark)
        
        if backwards:
            found, start_iter, end_iter = self.search_context.backward(cursor_iter)
        else:
            found, start_iter, end_iter = self.search_context.forward(cursor_iter)
        
        if found:
            # Select the found text
            self.buffer.select_range(start_iter, end_iter)
            
            # Scroll to the selection
            self.scroll_to_mark(self.buffer.get_insert(), 0.25, False, 0.5, 0.5)
            
            return True
        else:
            self.find_text(text, None, False)
            
            def on_replace(self, button):
                """Replace the current selection with the replacement text"""
                if not self.buffer.get_has_selection():
        return
        
                # Get the replacement text
                replacement = self.replace_entry.get_text()
                
                # Replace the selection
                self.buffer.begin_user_action()
                self.buffer.delete_selection(True, True)
                self.buffer.insert_at_cursor(replacement, -1)
                self.buffer.end_user_action()
                
                # Find the next occurrence
                self.on_find_next(None)
            
            def on_replace_all(self, button):
                """Replace all occurrences of the search text"""
                search_text = self.search_entry.get_text()
                if not search_text:
        return
        
                replacement = self.replace_entry.get_text()
                
                # Start from the beginning of the document
                start_iter = self.buffer.get_start_iter()
                
                # Begin batch action
                self.buffer.begin_user_action()
                
                # Count replacements
                count = 0
                
                # Search flags
                flags = GtkSource.SearchFlags.CASE_INSENSITIVE
                
                # Find all occurrences and replace
                while True:
        match, start_match, end_match = start_iter.forward_search(
            search_text, flags, None
        )
        
        if not match:
            break
            
        # Replace text
        self.buffer.delete(start_match, end_match)
        self.buffer.insert(start_match, replacement, -1)
        
        # Update search position
        start_iter = start_match.forward_chars(len(replacement))
        count += 1
                
                # End batch action
                self.buffer.end_user_action()
                
                # Notify user of replacements
                if count > 0:
        message = f"Replaced {count} occurrences"
        if hasattr(self.get_root(), "show_toast"):
            self.get_root().show_toast(message)
            
            def indent_selection(self):
                """Indent the selected text"""
                if not self.buffer.get_has_selection():
        return
        
                self.buffer.begin_user_action()
                
                start, end = self.buffer.get_selection_bounds()
                start_line = start.get_line()
                end_line = end.get_line()
                
                for line in range(start_line, end_line + 1):
        line_iter = self.buffer.get_iter_at_line(line)
        self.buffer.insert(line_iter, "  ", 2)  # Use 2 spaces
                
                self.buffer.end_user_action()
            
            def unindent_selection(self):
                """Unindent the selected text"""
                if not self.buffer.get_has_selection():
        return
        
                self.buffer.begin_user_action()
                
                start, end = self.buffer.get_selection_bounds()
                start_line = start.get_line()
                end_line = end.get_line()
                
                for line in range(start_line, end_line + 1):
        line_iter = self.buffer.get_iter_at_line(line)
        line_text = self.buffer.get_text(
            line_iter,
            self.buffer.get_iter_at_line_offset(line, min(4, line_iter.get_chars_in_line())),
            False
        )
        
        # Remove up to 2 spaces from the beginning of the line
        if line_text.startswith("  "):
            self.buffer.delete(line_iter, self.buffer.get_iter_at_line_offset(line, 2))
        elif line_text.startswith(" "):
            self.buffer.delete(line_iter, self.buffer.get_iter_at_line_offset(line, 1))
                
                self.buffer.end_user_action()
            
            def get_text(self):
                """Get the complete buffer text
                
                Returns:
        str: The text content
                """
                return self.buffer.get_text(
        self.buffer.get_start_iter(),
        self.buffer.get_end_iter(),
        False
                )
            
            def set_text(self, text):
                """Set the buffer text
                
                Args:
        text: Text to set
                """
                self.buffer.begin_user_action()
                self.buffer.set_text(text)
                self.buffer.end_user_action()
                self.buffer.set_modified(False)
            
            def is_modified(self):
                """Check if the buffer has been modified
                
                Returns:
        bool: True if modified, False otherwise
                """
                return self.buffer.get_modified()
            
            def set_modified(self, modified):
                """Set the buffer modified state
                
                Args:
        modified: Modified state
                """
                self.buffer.set_modified(modified)
                
            def set_language(self, language_name):
                """Set the buffer language
                
                Args:
        language_name: Language name (e.g., 'latex')
                """
                lang_manager = GtkSource.LanguageManager.get_default()
                language = lang_manager.get_language(language_name)
                if language:
        self.buffer.set_language(language)
            
            def set_font(self, font_description):
                """Set the editor font
                
                Args:
        font_description: Font description string
                """
                font = Pango.FontDescription.from_string(font_description)
                self.source_view.override_font(font)
            
            def set_theme(self, theme_name):
                """Set the editor color theme
                
                Args:
        theme_name: Theme name
                """
                style_manager = GtkSource.StyleSchemeManager.get_default()
                scheme = style_manager.get_scheme(theme_name)
                if scheme:
        self.buffer.set_style_scheme(scheme)
        
        
        class LaTeXCompletionProvider(GObject.Object, GtkSource.CompletionProvider):
            """LaTeX code completion provider"""
            
            def __init__(self):
                super().__init__()
                
                # Common LaTeX commands
                self.latex_commands = {
        "document": {
            "\\documentclass": "\\documentclass{article}",
            "\\title": "\\title{Document Title}",
            "\\author": "\\author{Author Name}",
            "\\date": "\\date{\\today}",
            "\\begin{document}": "\\begin{document}\n\n\\end{document}",
            "\\maketitle": "\\maketitle",
            "\\tableofcontents": "\\tableofcontents",
        },
        "sections": {
            "\\chapter": "\\chapter{Chapter Title}",
            "\\section": "\\section{Section Title}",
            "\\subsection": "\\subsection{Subsection Title}",
            "\\subsubsection": "\\subsubsection{Subsubsection Title}",
            "\\paragraph": "\\paragraph{Paragraph Title}",
            "\\subparagraph": "\\subparagraph{Subparagraph Title}",
        },
        "formatting": {
            "\\textbf": "\\textbf{bold text}",
            "\\textit": "\\textit{italic text}",
            "\\texttt": "\\texttt{monospace text}",
            "\\underline": "\\underline{underlined text}",
            "\\emph": "\\emph{emphasized text}",
        },
        "environments": {
            "\\begin{itemize}": "\\begin{itemize}\n\\item Item text\n\\end{itemize}",
            "\\begin{enumerate}": "\\begin{enumerate}\n\\item Item text\n\\end{enumerate}",
            "\\begin{equation}": "\\begin{equation}\n  E = mc^2\n\\end{equation}",
            "\\begin{align}": "\\begin{align}\n  y &= mx + b\n\\end{align}",
            "\\begin{figure}": "\\begin{figure}[htbp]\n  \\centering\n  \\includegraphics[width=0.8\\textwidth]{filename}\n  \\caption{Figure caption}\n  \\label{fig:label}\n\\end{figure}",
            "\\begin{table}": "\\begin{table}[htbp]\n  \\centering\n  \\begin{tabular}{cc}\n    Cell 1 & Cell 2 \\\\\n    Cell 3 & Cell 4 \\\\\n  \\end{tabular}\n  \\caption{Table caption}\n  \\label{tab:label}\n\\end{table}",
        },
        "math": {
            "\\frac": "\\frac{numerator}{denominator}",
            "\\sum": "\\sum_{i=1}^{n}",
            "\\int": "\\int_{a}^{b}",
            "\\sqrt": "\\sqrt{content}",
            "\\mathbf": "\\mathbf{vector}",
            "\\overline": "\\overline{expression}",
        },
        "references": {
            "\\cite": "\\cite{reference}",
            "\\label": "\\label{label}",
            "\\ref": "\\ref{label}",
            "\\eqref": "\\eqref{equation-label}",
            "\\pageref": "\\pageref{label}",
            "\\bibliography": "\\bibliography{bib-file}",
            "\\bibliographystyle": "\\bibliographystyle{style}",
        },
                }
            
            def do_get_name(self):
                """Get the provider name"""
                return "LaTeX Completion"
            
            def do_get_priority(self):
                """Get the provider priority"""
                return 1
            
            def get_completion_items(self, context):
                """Get completion items for the current context
                
                Args:
        context: The completion context
        
                Returns:
        list: List of completion items
                """
                # Get the current text
                buffer = context.get_buffer()
                start_iter = buffer.get_iter_at_mark(buffer.get_insert())
                
                # Get the line up to the cursor
                line_start = buffer.get_iter_at_line(start_iter.get_line())
                text_before = buffer.get_text(line_start, start_iter, False)
                
                # Look for partial commands
                match = re.search(r'\\[a-zA-Z]*$', text_before)
                if not match:
        return []
        
                # Get the partial command
                partial_command = match.group(0)
                
                # Find matching commands
                matches = []
                for category, commands in self.latex_commands.items():
        for cmd, completion in commands.items():
            if cmd.startswith(partial_command):
                matches.append((cmd, completion))
                
                # Create completion items
                items = []
                for cmd, completion in matches:
        item = GtkSource.CompletionItem.new()
        item.set_label(cmd)
        item.set_text(completion)
        items.append(item)
                
                return items
            # No more matches
            self.replace_activated = False
            return False
    
    def start_replace_all(self, search_text, replace_text, backwards=False, whole_word=False, match_case=False):
        """Replace all occurrences of search_text with replace_text
        
        Args:
            search_text: The text to search for
            replace_text: The replacement text
            backwards: Whether to search backwards (ignored for replace all)
            whole_word: Whether to match whole words only
            match_case: Whether to match case
            
        Returns:
            int: Number of replacements made
        """
        if not search_text:
            return 0
        
        # Create search settings
        settings = GtkSource.SearchSettings()
        settings.set_search_text(search_text)
        settings.set_case_sensitive(match_case)
        settings.set_at_word_boundaries(whole_word)
        settings.set_wrap_around(False)  # No need to wrap for replace all
        
        # Create search context
        context = GtkSource.SearchContext.new(self.buffer, settings)
        
        # Start from the beginning of the document
        start_iter = self.buffer.get_start_iter()
        
        count = 0
        self.buffer.begin_user_action()
        
        # Find all occurrences and replace them
        while True:
            found, match_start, match_end = context.forward(start_iter)
            if not found:
                break
            
            # Replace the text
            self.buffer.delete(match_start, match_end)
            self.buffer.insert(match_start, replace_text)
            
            # Move the iterator to after the replaced text
            start_iter = self.buffer.get_iter_at_offset(match_start.get_offset() + len(replace_text))
            
            count += 1
        
        self.buffer.end_user_action()
        
        # Deactivate replace mode
        self.replace_activated = False
        
        return count
    
    def undo(self):
        """Undo the last operation
        
        Returns:
            bool: True if an operation was undone, False otherwise
        """
        if self.buffer.can_undo():
            self.buffer.undo()
            return True
        return False
    
    def redo(self):
        """Redo the last undone operation
        
        Returns:
            bool: True if an operation was redone, False otherwise
        """
        if self.buffer.can_redo():
            self.buffer.redo()
            return True
        return False
    
    def get_cursor_position(self):
        """Get the current cursor position
        
        Returns:
            tuple: (line, column) - 1-based for display
        """
        cursor_mark = self.buffer.get_insert()
        cursor_iter = self.buffer.get_iter_at_mark(cursor_mark)
        line = cursor_iter.get_line() + 1  # Make 1-based for display
        column = cursor_iter.get_line_offset() + 1  # Make 1-based for display
        return (line, column)
    
    def goto_line(self, line):
        """Move the cursor to a specific line
        
        Args:
            line: The line number (1-based)
            
        Returns:
            bool: True if successful, False otherwise
        """
        if line < 1:
            return False
        
        # Convert to 0-based for internal use
        target_line = line - 1
        
        # Get the iterator for the line
        iter = self.buffer.get_iter_at_line(target_line)
        
        # Place the cursor at the beginning of the line
        self.buffer.place_cursor(iter)
        
        # Scroll to the cursor position
        self.scroll_to_mark(self.buffer.get_insert(), 0.25, False, 0.5, 0.5)
        
        return True


class LaTeXCompletionProvider(GObject.Object, GtkSource.CompletionProvider):
    """Completion provider for LaTeX commands"""
    
    def __init__(self):
        """Initialize the completion provider."""
        super().__init__()
        
        # Common LaTeX commands for autocompletion
        self.latex_commands = [
            "\\begin{}", "\\end{}", "\\cite{}", "\\ref{}", "\\label{}", 
            "\\textbf{}", "\\textit{}", "\\texttt{}", "\\underline{}", 
            "\\chapter{}", "\\section{}", "\\subsection{}", "\\subsubsection{}",
            "\\includegraphics{}", "\\bibliography{}", "\\bibliographystyle{}",
            "\\footnote{}", "\\centering", "\\maketitle", "\\tableofcontents",
            "\\documentclass{}", "\\usepackage{}", "\\newcommand{}", "\\renewcommand{}",
            "\\hspace{}", "\\vspace{}", "\\newpage", "\\clearpage",
            "\\frac{}{}", "\\sqrt{}", "\\sum", "\\prod", "\\int", "\\limits_{}^{}",
            "\\alpha", "\\beta", "\\gamma", "\\delta", "\\epsilon", "\\zeta", 
            "\\eta", "\\theta", "\\iota", "\\kappa", "\\lambda", "\\mu",
            "\\nu", "\\xi", "\\pi", "\\rho", "\\sigma", "\\tau", "\\upsilon",
            "\\phi", "\\chi", "\\psi", "\\omega"
        ]
    
    def do_get_name(self):
        """Get the provider name"""
        return "LaTeX Commands"
    
    def do_get_priority(self):
        """Get the provider priority"""
        return 1
    
    def do_match(self, context):
        """Check if the completion provider should be used in this context
        
        Args:
            context: The completion context
            
        Returns:
            bool: True if the provider should be used
        """
        # Get the iter at the completion position
        _, iter = context.get_bounds()
        
        # Check if we're typing a LaTeX command
        start = iter.copy()
        start.backward_chars(20)  # Look at the last 20 characters
        
        text = context.get_buffer().get_text(start, iter, False)
        return '\\' in text
    
    def do_populate(self, context):
        """Populate the completion with LaTeX commands
        
        Args:
            context: The completion context
        """
        _, end = context.get_bounds()
        start = end.copy()
        
        # Find the start of the current command
        while True:
            if start.get_offset() == 0 or start.get_char() == '\\':
                break
            if not start.backward_char():
                break
        
        if start.get_char() != '\\':
            return
        
        # Get the partial command
        buffer = context.get_buffer()
        partial_command = buffer.get_text(start, end, False)
        
        # Create a list of proposals
        proposals = []
        
        for command in self.latex_commands:
            if command.startswith(partial_command):
                proposal = GtkSource.CompletionItem.new()
                proposal.set_label(command)
                proposal.set_text(command)
                proposals.append(proposal)
        
        # Add the proposals to the context
        context.add_proposals(self, proposals, True)
