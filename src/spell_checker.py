# spell_checker.py - Spell checking for LaTeX documents
import gi
import os
import re

gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource, GObject, Gdk, GLib, Pango

try:
    import enchant
    from enchant.checker import SpellChecker
    ENCHANT_AVAILABLE = True
except ImportError:
    ENCHANT_AVAILABLE = False


class LatexSpellChecker(GObject.Object):
    """Spell checker for LaTeX documents"""
    
    def __init__(self, config):
        """Initialize the spell checker"""
        super().__init__()
        
        self.config = config
        self.language = "en_US"  # Default language
        self.enabled = False
        self.checker = None
        self.misspelled_words = {}  # Maps word to a list of positions
        self.text_buffer = None
        self.highlight_tag = None
        self.idle_check_id = 0
        
        # Initialize if enchant is available
        if ENCHANT_AVAILABLE:
            # Try to get language from locale
            try:
                import locale
                loc = locale.getlocale()[0]
                if loc and enchant.dict_exists(loc):
                    self.language = loc
            except (ImportError, Exception):
                pass
            
            try:
                self.checker = SpellChecker(self.language)
                self.enabled = True
            except Exception as e:
                print(f"Error initializing spell checker: {e}")
                self.enabled = False
    
    def set_language(self, language):
        """Set spell check language"""
        if not ENCHANT_AVAILABLE:
            return False
        
        if not enchant.dict_exists(language):
            return False
        
        try:
            self.checker = SpellChecker(language)
            self.language = language
            
            # If attached to a buffer, recheck
            if self.text_buffer:
                self.check_buffer()
            
            return True
        except Exception as e:
            print(f"Error setting spell check language: {e}")
            return False
    
    def set_enabled(self, enabled):
        """Enable or disable spell checking"""
        if enabled and not ENCHANT_AVAILABLE:
            return False
        
        self.enabled = enabled and ENCHANT_AVAILABLE
        
        # If disabling, remove all highlights
        if not self.enabled and self.text_buffer and self.highlight_tag:
            start, end = self.text_buffer.get_bounds()
            self.text_buffer.remove_tag(self.highlight_tag, start, end)
        
        # If enabling, check the buffer
        if self.enabled and self.text_buffer:
            self.check_buffer()
        
        return True
    
    def get_available_languages(self):
        """Get available spell check languages"""
        if not ENCHANT_AVAILABLE:
            return []
        
        try:
            return enchant.list_languages()
        except Exception:
            return []
    
    def is_enabled(self):
        """Check if spell checking is enabled"""
        return self.enabled and ENCHANT_AVAILABLE
    
    def attach_buffer(self, text_buffer):
        """Attach to a text buffer for spell checking"""
        if self.text_buffer == text_buffer:
            return
        
        # Clean up previous buffer
        if self.text_buffer and self.highlight_tag:
            start, end = self.text_buffer.get_bounds()
            self.text_buffer.remove_tag(self.highlight_tag, start, end)
        
        self.text_buffer = text_buffer
        
        # Create highlight tag if needed
        if not self.highlight_tag and self.text_buffer:
            self.highlight_tag = self.text_buffer.create_tag(
                "misspelled",
                underline=Pango.Underline.ERROR,
                underline_rgba=Gdk.RGBA(1.0, 0.0, 0.0, 1.0)
            )
        
        # Connect to buffer changes
        if self.text_buffer:
            self.text_buffer.connect("changed", self.on_buffer_changed)
            
            # Check the buffer
            if self.enabled:
                self.check_buffer()
    
    def on_buffer_changed(self, buffer):
        """Handle buffer content changes"""
        if not self.enabled:
            return
        
        # Cancel any pending check
        if self.idle_check_id > 0:
            GLib.source_remove(self.idle_check_id)
        
        # Schedule a new check
        self.idle_check_id = GLib.timeout_add(500, self.check_buffer)
    
    def check_buffer(self):
        """Check the buffer for misspelled words"""
        if not self.enabled or not self.text_buffer or not self.checker:
            self.idle_check_id = 0
            return False
        
        # Remove existing spell check highlights
        start, end = self.text_buffer.get_bounds()
        self.text_buffer.remove_tag(self.highlight_tag, start, end)
        
        # Get text
        text = self.text_buffer.get_text(start, end, True)
        
        # Filter out LaTeX commands and environments
        filtered_text, word_positions = self.filter_latex(text)
        
        # Check spelling
        self.checker.set_text(filtered_text)
        for error in self.checker:
            word = error.word
            word_start = error.wordpos
            word_end = word_start + len(word)
            
            # Convert position back to original text
            if word_start in word_positions:
                orig_start = word_positions[word_start]
                
                # Find closest position for end
                orig_end = None
                for pos in sorted(word_positions.keys()):
                    if pos >= word_end:
                        orig_end = word_positions[pos]
                        break
                
                if orig_end is None:
                    # Use buffer end as fallback
                    orig_end = len(text)
                
                # Apply highlight
                start_iter = self.text_buffer.get_iter_at_offset(orig_start)
                end_iter = self.text_buffer.get_iter_at_offset(orig_end)
                self.text_buffer.apply_tag(self.highlight_tag, start_iter, end_iter)
        
        self.idle_check_id = 0
        return False
    
    def filter_latex(self, text):
        """Filter LaTeX commands and environments from text for spell checking"""
        # This is a simplified implementation
        # A more comprehensive one would properly handle more LaTeX constructs
        
        filtered_text = ""
        word_positions = {}  # Maps position in filtered text to position in original text
        
        # Patterns to ignore
        ignore_patterns = [
            r'\\[a-zA-Z]+(\{[^\}]*\})*',  # LaTeX commands with arguments
            r'\$[^\$]*\$',  # Inline math
            r'\$\$[^\$]*\$\$',  # Display math
            r'\\begin\{[^\}]*\}.*?\\end\{[^\}]*\}',  # Environments
            r'%.*?$'  # Comments
        ]
        
        # Replace patterns with spaces to preserve word positions
        filtered = text
        for pattern in ignore_patterns:
            filtered = re.sub(pattern, lambda m: ' ' * len(m.group(0)), filtered, flags=re.MULTILINE | re.DOTALL)
        
        # Map positions
        filtered_pos = 0
        for orig_pos, char in enumerate(filtered):
            if not char.isspace():
                word_positions[filtered_pos] = orig_pos
                filtered_text += char
                filtered_pos += 1
        
        return filtered_text, word_positions
    
    def get_suggestions(self, word):
        """Get spelling suggestions for a word"""
        if not self.enabled or not self.checker:
            return []
        
        try:
            return self.checker.suggest(word)
        except Exception:
            return []
    
    def add_to_dictionary(self, word):
        """Add a word to the user dictionary"""
        if not self.enabled or not self.checker:
            return False
        
        try:
            self.checker.add_to_personal(word)
            
            # Recheck the buffer
            if self.text_buffer:
                self.check_buffer()
            
            return True
        except Exception:
            return False
    
    def show_context_menu(self, view, event):
        """Show spell check context menu with suggestions"""
        if not self.enabled or not self.text_buffer:
            return False
        
        # Get cursor position
        x, y = event.get_coords()
        buffer_x, buffer_y = view.window_to_buffer_coords(Gtk.TextWindowType.TEXT, x, y)
        
        # Get the iter at position
        iter_at_position = view.get_iter_at_position(buffer_x, buffer_y)
        if not iter_at_position:
            return False
        
        text_iter = iter_at_position[1]
        
        # Check if the position has a misspelled tag
        tags = text_iter.get_tags()
        if not self.highlight_tag in tags:
            return False
        
        # Get the misspelled word
        word_start = text_iter.copy()
        word_end = text_iter.copy()
        
        # Find word boundaries
        if not word_start.starts_word():
            word_start.backward_word_start()
        
        if not word_end.ends_word():
            word_end.forward_word_end()
        
        word = self.text_buffer.get_text(word_start, word_end, False)
        
        # Create the menu
        menu = Gtk.PopoverMenu()
        menu.set_parent(view)
        
        # Set position
        rect = Gdk.Rectangle()
        rect.x = buffer_x
        rect.y = buffer_y
        rect.width = 1
        rect.height = 1
        menu.set_pointing_to(rect)
        
        # Create menu model
        menu_model = Gio.Menu()
        
        # Add suggestions
        suggestions = self.get_suggestions(word)
        if suggestions:
            suggestions_menu = Gio.Menu()
            for i, suggestion in enumerate(suggestions[:10]):  # Limit to 10 suggestions
                action_name = f"suggestion{i}"
                action = Gio.SimpleAction.new(action_name, None)
                action.connect("activate", lambda a, p, w=suggestion, s=word_start, e=word_end: self.replace_word(w, s, e))
                view.get_action_group("spell").add_action(action)
                
                suggestions_menu.append(suggestion, f"spell.{action_name}")
            
            menu_model.append_submenu("Suggestions", suggestions_menu)
        else:
            menu_model.append("(No suggestions)", "spell.none")
            none_action = Gio.SimpleAction.new("none", None)
            none_action.set_enabled(False)
            view.get_action_group("spell").add_action(none_action)
        
        # Add dictionary action
        add_action = Gio.SimpleAction.new("add", None)
        add_action.connect("activate", lambda a, p, w=word: self.add_to_dictionary_and_recheck(w))
        view.get_action_group("spell").add_action(add_action)
        menu_model.append(f"Add \"{word}\" to Dictionary", "spell.add")
        
        # Set the menu model
        menu.set_menu_model(menu_model)
        
        # Show the menu
        menu.popup()
        return True
    
    def replace_word(self, new_word, start_iter, end_iter):
        """Replace a misspelled word with a suggestion"""
        if not self.text_buffer:
            return
        
        self.text_buffer.begin_user_action()
        self.text_buffer.delete(start_iter, end_iter)
        self.text_buffer.insert(start_iter, new_word)
        self.text_buffer.end_user_action()
    
    def add_to_dictionary_and_recheck(self, word):
        """Add a word to dictionary and recheck the buffer"""
        if self.add_to_dictionary(word):
            self.check_buffer()
