# editor_view.py - LaTeX editor view component
import gi
# editor_view.py - LaTeX source editor component
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')

from gi.repository import Gtk, GtkSource, GObject, Gdk, Pango


class LatexEditorView(Gtk.Box):
    """LaTeX source editor component"""
    
    __gsignals__ = {
        'modified-changed': (GObject.SignalFlags.RUN_FIRST, None, (bool,)),
        'content-changed': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
        'cursor-position-changed': (GObject.SignalFlags.RUN_FIRST, None, (int, int)),
    }
    
    def __init__(self, config):
        """Initialize the editor component"""
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        self.config = config
        self._update_content_timeout = 0
        
        # Create source buffer
        self.buffer = GtkSource.Buffer()
        
        # Set up language (LaTeX)
        lang_manager = GtkSource.LanguageManager.get_default()
        latex_lang = lang_manager.get_language('latex')
        if latex_lang:
            self.buffer.set_language(latex_lang)
        
        # Set up style scheme
        style_manager = GtkSource.StyleSchemeManager.get_default()
        
        # Attempt to get a style that works well in both light/dark modes
        style_scheme = style_manager.get_scheme('Adwaita')
        if not style_scheme:
            # Try other common schemes
            scheme_names = style_manager.get_scheme_ids()
            if 'classic' in scheme_names:
                style_scheme = style_manager.get_scheme('classic')
            elif 'tango' in scheme_names:
                style_scheme = style_manager.get_scheme('tango')
            elif len(scheme_names) > 0:
                style_scheme = style_manager.get_scheme(scheme_names[0])
        
        if style_scheme:
            self.buffer.set_style_scheme(style_scheme)
        
        # Create source view
        self.view = GtkSource.View(
            buffer=self.buffer,
            monospace=True,
            show_line_numbers=config.get_boolean('editor-show-line-numbers'),
            highlight_current_line=config.get_boolean('editor-highlight-current-line'),
            show_right_margin=config.get_boolean('editor-show-right-margin'),
            right_margin_position=config.get_int('editor-right-margin-position'),
            wrap_mode=Gtk.WrapMode.WORD if config.get_boolean('editor-wrap-text') else Gtk.WrapMode.NONE,
            auto_indent=config.get_boolean('editor-auto-indent'),
            insert_spaces_instead_of_tabs=config.get_boolean('editor-use-spaces'),
            tab_width=config.get_int('editor-tab-width'),
            smart_backspace=True,
            smart_home_end=GtkSource.SmartHomeEndType.AFTER
        )
        
        # Set font
        font_desc = Pango.FontDescription.from_string(config.get_string('editor-font'))
        self.view.override_font(font_desc)
        
        # Create scrolled window
        sw = Gtk.ScrolledWindow()
        sw.set_vexpand(True)
        sw.set_child(self.view)
        self.append(sw)
        
        # Connect to buffer signals
        self.buffer.connect('modified-changed', self.on_modified_changed)
        self.buffer.connect('changed', self.on_buffer_changed)
        
        # Connect to cursor position change
        self.view.connect('move-cursor', self.on_move_cursor)
        self.buffer.connect('notify::cursor-position', self.on_cursor_position_changed)
        
        # Bind configuration settings
        self.bind_config_settings()
    
    def bind_config_settings(self):
        """Bind editor settings to configuration"""
        # Connect to config changes
        self.config.connect('editor-show-line-numbers', lambda *_: 
            self.view.set_show_line_numbers(self.config.get_boolean('editor-show-line-numbers')))
        
        self.config.connect('editor-highlight-current-line', lambda *_: 
            self.view.set_highlight_current_line(self.config.get_boolean('editor-highlight-current-line')))
        
        self.config.connect('editor-show-right-margin', lambda *_: 
            self.view.set_show_right_margin(self.config.get_boolean('editor-show-right-margin')))
        
        self.config.connect('editor-right-margin-position', lambda *_: 
            self.view.set_right_margin_position(self.config.get_int('editor-right-margin-position')))
        
        self.config.connect('editor-wrap-text', lambda *_: 
            self.view.set_wrap_mode(Gtk.WrapMode.WORD if self.config.get_boolean('editor-wrap-text') 
                                    else Gtk.WrapMode.NONE))
        
        self.config.connect('editor-tab-width', lambda *_: 
            self.view.set_tab_width(self.config.get_int('editor-tab-width')))
        
        self.config.connect('editor-use-spaces', lambda *_: 
            self.view.set_insert_spaces_instead_of_tabs(self.config.get_boolean('editor-use-spaces')))
        
        self.config.connect('editor-font', lambda *_: 
            self.view.override_font(Pango.FontDescription.from_string(self.config.get_string('editor-font'))))
    
    def on_modified_changed(self, buffer):
        """Handle modified state changes"""
        modified = buffer.get_modified()
        self.emit('modified-changed', modified)
    
    def on_buffer_changed(self, buffer):
        """Handle text changes in the buffer"""
        # Emit content-changed with a slight delay to avoid excessive processing
        if self._update_content_timeout > 0:
            from gi.repository import GLib
            GLib.source_remove(self._update_content_timeout)
        
        from gi.repository import GLib
        self._update_content_timeout = GLib.timeout_add(300, self.emit_content_changed)
    
    def emit_content_changed(self):
        """Emit content-changed signal with current text"""
        content = self.get_text()
        self.emit('content-changed', content)
        self._update_content_timeout = 0
        return False  # Don't repeat
    
    def on_move_cursor(self, view, step, count, extend_selection):
        """Handle cursor movement"""
        # Cursor position is handled by on_cursor_position_changed
        pass
    
    def on_cursor_position_changed(self, buffer, param_spec):
        """Handle cursor position changes"""
        cursor_mark = buffer.get_insert()
        cursor_iter = buffer.get_iter_at_mark(cursor_mark)
        
        line = cursor_iter.get_line() + 1  # Lines are 0-based in GTK
        column = cursor_iter.get_line_offset() + 1  # Columns are 0-based in GTK
        
        self.emit('cursor-position-changed', line, column)
    
    def set_text(self, text):
        """Set the editor text"""
        self.buffer.begin_not_undoable_action()
        self.buffer.set_text(text)
        self.buffer.end_not_undoable_action()
        self.buffer.set_modified(False)
    
    def get_text(self):
        """Get the editor text"""
        start, end = self.buffer.get_bounds()
        return self.buffer.get_text(start, end, False)
    
    def scroll_to_line(self, line):
        """Scroll to a specific line"""
        line = max(0, line - 1)  # Convert to 0-based
        iter = self.buffer.get_iter_at_line(line)
        self.buffer.place_cursor(iter)
        self.view.scroll_to_iter(iter, 0.0, True, 0.5, 0.5)
    
    def zoom_in(self):
        """Increase font size"""
        font_string = self.config.get_string('editor-font')
        font_desc = Pango.FontDescription.from_string(font_string)
        size = font_desc.get_size() / Pango.SCALE
        
        if size < 72:  # Limit to reasonable size
            size += 1
            font_desc.set_size(size * Pango.SCALE)
            new_font_string = font_desc.to_string()
            self.config.set_string('editor-font', new_font_string)
            self.view.override_font(font_desc)
    
    def zoom_out(self):
        """Decrease font size"""
        font_string = self.config.get_string('editor-font')
        font_desc = Pango.FontDescription.from_string(font_string)
        size = font_desc.get_size() / Pango.SCALE
        
        if size > 6:  # Don't let it get too small
            size -= 1
            font_desc.set_size(size * Pango.SCALE)
            new_font_string = font_desc.to_string()
            self.config.set_string('editor-font', new_font_string)
            self.view.override_font(font_desc)
    
    def zoom_reset(self):
        """Reset font size to default"""
        # Assume default is "Monospace 11"
        default_font = "Monospace 11"
        font_desc = Pango.FontDescription.from_string(default_font)
        self.config.set_string('editor-font', default_font)
        self.view.override_font(font_desc)
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')

from gi.repository import Gtk, Adw, GtkSource, GObject, Gdk, Pango

class LatexEditorView(GtkSource.View):
    """LaTeX editor view with syntax highlighting and advanced editing features"""
    
    __gsignals__ = {
        'content-changed': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
        'modification-state-changed': (GObject.SignalFlags.RUN_FIRST, None, (bool,)),
    }
    
    def __init__(self):
        super().__init__()
        
        # Create a buffer with LaTeX syntax highlighting
        language_manager = GtkSource.LanguageManager.get_default()
        self.buffer = GtkSource.Buffer.new_with_language(language_manager.get_language('latex'))
        self.set_buffer(self.buffer)
        
        # Set up style scheme
        style_scheme_manager = GtkSource.StyleSchemeManager.get_default()
        self.buffer.set_style_scheme(style_scheme_manager.get_scheme('kate'))
        
        # Set up editor properties
        self.set_monospace(True)
        self.set_show_line_numbers(True)
        self.set_tab_width(4)
        self.set_auto_indent(True)
        self.set_indent_on_tab(True)
        self.set_insert_spaces_instead_of_tabs(True)
        self.set_smart_backspace(True)
        self.set_highlight_current_line(True)
        self.set_wrap_mode(Gtk.WrapMode.WORD)
        
        # Add margin indicator
        self.set_right_margin(80)
        self.set_show_right_margin(True)
        
        # Set default font
        self.set_font("Monospace 11")
        
        # Set up completion
        self.setup_completion()
        
        # Connect signals
        self.buffer.connect('changed', self.on_buffer_changed)
        self.buffer.connect('modified-changed', self.on_buffer_modified_changed)
        
        # Setup keyboard shortcuts
        self.setup_key_controller()
    
    def setup_completion(self):
        """Set up code completion"""
        # Create completion provider
        self.completion = GtkSource.Completion.new()
        
        # Add LaTeX completion provider
        provider = LatexCompletionProvider()
        self.completion.add_provider(provider)
    
    def setup_key_controller(self):
        """Set up key event handling for shortcuts"""
        key_controller = Gtk.EventControllerKey.new()
        key_controller.connect('key-pressed', self.on_key_pressed)
        self.add_controller(key_controller)
    
    def on_key_pressed(self, controller, keyval, keycode, state):
        """Handle key press events for custom shortcuts"""
        # Tab indentation for selected text
        if keyval == Gdk.KEY_Tab and self.buffer.get_has_selection():
            self.indent_selection()
            return True
        
        # Shift+Tab unindentation for selected text
        if keyval == Gdk.KEY_ISO_Left_Tab and self.buffer.get_has_selection():
            self.unindent_selection()
            return True
        
        # Auto-close brackets
        if keyval in [Gdk.KEY_parenleft, Gdk.KEY_braceleft, Gdk.KEY_bracketleft]:
            closing_char = {
                Gdk.KEY_parenleft: ')',
                Gdk.KEY_braceleft: '}',
                Gdk.KEY_bracketleft: ']'
            }[keyval]
            
            # Insert the closing character and place cursor between
            self.buffer.begin_user_action()
            self.buffer.insert_at_cursor(chr(keyval) + closing_char, 2)
            cursor_pos = self.buffer.get_property('cursor-position')
            self.buffer.place_cursor(self.buffer.get_iter_at_offset(cursor_pos - 1))
            self.buffer.end_user_action()
            return True
        
        return False
    
    def indent_selection(self):
        """Indent the selected text"""
        self.buffer.begin_user_action()
        
        start, end = self.buffer.get_selection_bounds()
        start_line = start.get_line()
        end_line = end.get_line()
        
        for line in range(start_line, end_line + 1):
            line_iter = self.buffer.get_iter_at_line(line)
            self.buffer.insert(line_iter, "    ")
        
        self.buffer.end_user_action()
    
    def unindent_selection(self):
        """Unindent the selected text"""
        self.buffer.begin_user_action()
        
        start, end = self.buffer.get_selection_bounds()
        start_line = start.get_line()
        end_line = end.get_line()
        
        for line in range(start_line, end_line + 1):
            line_iter = self.buffer.get_iter_at_line(line)
            line_text = self.buffer.get_text(
                line_iter,
                self.buffer.get_iter_at_line_offset(line, 4),
                False
            )
            
            # Remove up to 4 spaces from the beginning of the line
            if line_text.startswith("    "):
                self.buffer.delete(line_iter, self.buffer.get_iter_at_line_offset(line, 4))
            elif line_text.startswith("   "):
                self.buffer.delete(line_iter, self.buffer.get_iter_at_line_offset(line, 3))
            elif line_text.startswith("  "):
                self.buffer.delete(line_iter, self.buffer.get_iter_at_line_offset(line, 2))
            elif line_text.startswith(" "):
                self.buffer.delete(line_iter, self.buffer.get_iter_at_line_offset(line, 1))
        
        self.buffer.end_user_action()
    
    def set_font(self, font_str):
        """Set the editor font"""
        font_desc = Pango.FontDescription.from_string(font_str)
        self.override_font(font_desc)
    
    def on_buffer_changed(self, buffer):
        """Handle buffer content changes"""
        # Emit signal with new content
        text = buffer.get_text(
            buffer.get_start_iter(),
            buffer.get_end_iter(),
            False
        )
        self.emit('content-changed', text)
    
    def on_buffer_modified_changed(self, buffer):
        """Handle buffer modification state changes"""
        modified = buffer.get_modified()
        self.emit('modification-state-changed', modified)
    
    def get_text(self):
        """Get the text content of the editor"""
        return self.buffer.get_text(
            self.buffer.get_start_iter(),
            self.buffer.get_end_iter(),
            False
        )
    
    def set_text(self, text):
        """Set the text content of the editor"""
        self.buffer.begin_user_action()
        self.buffer.set_text(text)
        self.buffer.end_user_action()
    
    def is_modified(self):
        """Check if buffer has unsaved modifications"""
        return self.buffer.get_modified()


class LatexCompletionProvider(GObject.Object, GtkSource.CompletionProvider):
    """LaTeX code completion provider"""
    
    LATEX_COMMANDS = [
        # Document structure
        '\\documentclass', '\\usepackage', '\\begin', '\\end', '\\section',
        '\\subsection', '\\subsubsection', '\\paragraph', '\\title', '\\author',
        '\\date', '\\maketitle', '\\tableofcontents', '\\appendix',
        
        # Formatting
        '\\textbf', '\\textit', '\\texttt', '\\underline', '\\emph',
        '\\footnote', '\\cite', '\\ref', '\\label', '\\pageref',
        
        # Math
        '\\equation', '\\align', '\\sum', '\\prod', '\\int', '\\frac',
        '\\alpha', '\\beta', '\\gamma', '\\delta', '\\epsilon', '\\theta',
        '\\lambda', '\\mu', '\\pi', '\\sigma', '\\tau', '\\phi', '\\omega',
        
        # Environments
        'figure', 'table', 'tabular', 'itemize', 'enumerate', 'description',
        'quote', 'verbatim', 'equation', 'align', 'theorem', 'proof',
        
        # Other common commands
        '\\item', '\\hspace', '\\vspace', '\\newpage', '\\linebreak', '\\noindent',
        '\\includegraphics', '\\caption', '\\centering', '\\textwidth'
    ]
    
    def __init__(self):
        super().__init__()
    
    def do_get_name(self):
        return "LaTeX"
    
    def do_get_priority(self):
        return 1
    
    def do_get_icon(self):
        return None
    
    def do_populate(self, context):
        # Get the current typed text
        iter_start = context.get_iter()
        buffer = context.get_buffer()
        
        # Find word start
        if not iter_start.starts_line():
            iter_start.backward_char()
            while not iter_start.starts_line():
                char = iter_start.get_char()
                if char.isspace():
                    iter_start.forward_char()
                    break
                iter_start.backward_char()
                if iter_start.starts_line():
                    break
            
            if not iter_start.starts_line():
                iter_start.forward_char()
        
        # Get the text from word start to current position
        typed_text = buffer.get_text(iter_start, context.get_iter(), False)
        
        # If we have text to match
        if typed_text and len(typed_text) >= 1:
            # Create completion proposals for matching commands
            proposals = []
            for cmd in self.LATEX_COMMANDS:
                if cmd.startswith(typed_text):
                    proposal = GtkSource.CompletionItem.new()
                    proposal.set_label(cmd)
                    proposal.set_text(cmd)
                    proposals.append(proposal)
            
            # Add proposals to context
            context.add_proposals(self, proposals, True)
