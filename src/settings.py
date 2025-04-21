import gi
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gdk, GLib
import os
import configparser

class SilktexSettings:
    def __init__(self):
        # Define config file path
        self.config_dir = os.path.join(GLib.get_user_config_dir(), "silktex")
        self.config_file = os.path.join(self.config_dir, "config.ini")

        # Default settings
        self.defaults = {
            "Editor": {
                "font_size": 12,
                "line_numbers": True,
                "wrap_text": True,
                "highlight_current_line": True,
                "tab_width": 4,
                "use_spaces": True,
                "auto_indent": True
            },
            "Preview": {
                "auto_preview": True,
                "preview_delay": 1000,  # ms
                "zoom_level": 1.0
            },
            "Compilation": {
                "latex_command": "pdflatex",
                "bibtex_enabled": True,
                "shell_escape": False,
                "additional_args": ""
            },
            "Interface": {
                "theme": "system",
                "paned_position": 400,
                "sidebar_width": 250
            }
        }

        # Ensure config directory exists
        if not os.path.exists(self.config_dir):
            os.makedirs(self.config_dir)

        # Load or create config
        self.config = configparser.ConfigParser()

        if os.path.exists(self.config_file):
            self.config.read(self.config_file)
        else:
            self.reset_to_defaults()

    def reset_to_defaults(self):
        """Reset all settings to default values"""
        self.config = configparser.ConfigParser()

        for section, options in self.defaults.items():
            self.config[section] = {}
            for key, value in options.items():
                # Convert boolean and other types to string
                if isinstance(value, bool):
                    self.config[section][key] = str(value).lower()
                elif isinstance(value, (int, float)):
                    self.config[section][key] = str(value)
                else:
                    self.config[section][key] = value

        self.save()

    def save(self):
        """Save current configuration to file"""
        with open(self.config_file, 'w') as f:
            self.config.write(f)

    # Get methods with type conversion
    def get_string(self, section, key, fallback=None):
        """Get a string setting value"""
        if section not in self.config:
            return self.get_default(section, key)
        return self.config.get(section, key, fallback=fallback)

    def get_boolean(self, section, key, fallback=None):
        """Get a boolean setting value"""
        if section not in self.config:
            return self.get_default(section, key)

        try:
            return self.config.getboolean(section, key, fallback=fallback)
        except ValueError:
            return self.get_default(section, key)

    def get_integer(self, section, key, fallback=None):
        """Get an integer setting value"""
        if section not in self.config:
            return self.get_default(section, key)

        try:
            return self.config.getint(section, key, fallback=fallback)
        except ValueError:
            return self.get_default(section, key)

    def get_float(self, section, key, fallback=None):
        """Get a float setting value"""
        if section not in self.config:
            return self.get_default(section, key)

        try:
            return self.config.getfloat(section, key, fallback=fallback)
        except ValueError:
            return self.get_default(section, key)

    def get_default(self, section, key):
        """Get default value for a setting"""
        if section in self.defaults and key in self.defaults[section]:
            return self.defaults[section][key]
        return None

    # Set methods with validation
    def set_string(self, section, key, value):
        """Set a string setting value"""
        self._ensure_section(section)
        self.config[section][key] = value

    def set_boolean(self, section, key, value):
        """Set a boolean setting value"""
        self._ensure_section(section)
        self.config[section][key] = str(value).lower()

    def set_integer(self, section, key, value):
        """Set an integer setting value"""
        self._ensure_section(section)
        self.config[section][key] = str(int(value))

    def set_float(self, section, key, value):
        """Set a float setting value"""
        self._ensure_section(section)
        self.config[section][key] = str(float(value))

    def _ensure_section(self, section):
        """Ensure a section exists in the config"""
        if section not in self.config:
            self.config[section] = {}

# Create a singleton instance for global access
settings = SilktexSettings()

# Helper functions
def apply_editor_settings(editor):
    """Apply editor settings to a GtkTextView"""
    # Text wrapping
    if settings.get_boolean("Editor", "wrap_text"):
        editor.set_wrap_mode(Gtk.WrapMode.WORD)
    else:
        editor.set_wrap_mode(Gtk.WrapMode.NONE)

    # Set margins based on settings
    editor.set_top_margin(10)
    editor.set_bottom_margin(10)
    editor.set_left_margin(10)
    editor.set_right_margin(10)

def get_latex_command():
    """Get the configured LaTeX command with any additional arguments"""
    command = settings.get_string("Compilation", "latex_command")
    additional_args = settings.get_string("Compilation", "additional_args")

    if settings.get_boolean("Compilation", "shell_escape"):
        additional_args += " -shell-escape"

    return f"{command} {additional_args}".strip()
