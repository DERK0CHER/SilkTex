# config.py - Configuration handling
import os
import json
import gi
# config.py - Configuration manager for SilkTex
import os
import gi
#!/usr/bin/env python3
# config.py - Configuration management for SilkTex

import os
import json

class ConfigManager:
    """Manages application configuration"""
    
    def __init__(self, config_file=None):
        """Initialize the configuration manager
        
        Args:
            config_file: Optional path to a configuration file
        """
        self.config_file = config_file or os.path.expanduser("~/.config/silktex/config.json")
        self.config = {
            'color-scheme': 'default',
            'editor': {
                'font': 'Monospace 11',
                'show-line-numbers': True,
                'highlight-current-line': True,
                'auto-indent': True,
                'tab-width': 4,
                'use-spaces': True,
                'word-wrap': True,
                'right-margin': 80,
                'show-right-margin': True
            },
            'latex': {
                'compiler': 'pdflatex',
                'auto-compile': True,
                'compile-timeout': 5000
            },
            'ui': {
                'sidebar-width': 250,
                'preview-width': 500,
                'show-sidebar': True,
                'show-statusbar': True
            }
        }
        
        # Create directory if needed
        os.makedirs(os.path.dirname(self.config_file), exist_ok=True)
        
        # Load configuration if file exists
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r') as f:
                    loaded_config = json.load(f)
                    # Update config with loaded values
                    self._update_config(self.config, loaded_config)
            except (json.JSONDecodeError, IOError) as e:
                print(f"Error loading configuration: {e}")
    
    def _update_config(self, config, updates):
        """Recursively update configuration dictionary
        
        Args:
            config: Configuration dictionary to update
            updates: Dictionary with updates
        """
        for key, value in updates.items():
            if key in config and isinstance(config[key], dict) and isinstance(value, dict):
                self._update_config(config[key], value)
            else:
                config[key] = value
    
    def save(self):
        """Save configuration to file"""
        try:
            with open(self.config_file, 'w') as f:
                json.dump(self.config, f, indent=2)
        except IOError as e:
            print(f"Error saving configuration: {e}")
    
    def get_string(self, key, default=None):
        """Get a string configuration value
        
        Args:
            key: Configuration key (dot-separated for nested keys)
            default: Default value if key not found
            
        Returns:
            String value or default
        """
        return self._get_value(key, default)
    
    def get_boolean(self, key, default=False):
        """Get a boolean configuration value
        
        Args:
            key: Configuration key (dot-separated for nested keys)
            default: Default value if key not found
            
        Returns:
            Boolean value or default
        """
        value = self._get_value(key, default)
        if isinstance(value, bool):
            return value
        return default
    
    def get_integer(self, key, default=0):
        """Get an integer configuration value
        
        Args:
            key: Configuration key (dot-separated for nested keys)
            default: Default value if key not found
            
        Returns:
            Integer value or default
        """
        value = self._get_value(key, default)
        try:
            return int(value)
        except (ValueError, TypeError):
            return default
    
    def set_value(self, key, value):
        """Set a configuration value
        
        Args:
            key: Configuration key (dot-separated for nested keys)
            value: Value to set
        """
        keys = key.split('.')
        config = self.config
        
        # Navigate to the correct level
        for k in keys[:-1]:
            if k not in config:
                config[k] = {}
            config = config[k]
        
        # Set the value
        config[keys[-1]] = value
    
    def _get_value(self, key, default=None):
        """Get a configuration value
        
        Args:
            key: Configuration key (dot-separated for nested keys)
            default: Default value if key not found
            
        Returns:
            Configuration value or default
        """
        keys = key.split('.')
        config = self.config
        
        # Navigate to the correct level
        for k in keys:
            if not isinstance(config, dict) or k not in config:
                return default
            config = config[k]
        
        return config
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, GLib, Gio


class ConfigManager:
    """Configuration manager for SilkTex application"""
    
    def __init__(self):
        """Initialize configuration manager"""
        # Default values for settings
        self.defaults = {
            # Interface settings
            'color-scheme': 'system',  # system, light, dark
            'sidebar-visible': True,
            
            # Window settings
            'window-width': 1200,
            'window-height': 800,
            'is-maximized': False,
            'is-fullscreen': False,
            'editor-preview-split': 600,  # Split position
            
            # Editor settings
            'editor-font': 'Monospace 11',
            'editor-show-line-numbers': True,
            'editor-highlight-current-line': True,
            'editor-show-right-margin': True,
            'editor-right-margin-position': 80,
            'editor-wrap-text': True,
            'editor-auto-indent': True,
            'editor-use-spaces': True,
            'editor-tab-width': 4,
            
            # LaTeX settings
            'latex-engine': 'pdflatex',  # pdflatex, xelatex, lualatex
            'latex-shell-escape': False,
            'latex-auto-compile': True,
            
            # Preview settings
            'preview-auto-refresh': True,
            'preview-refresh-delay': 1000,  # ms
            'preview-zoom-level': 1.0,
        }
        
        # Load settings
        self.settings = self._load_settings()
    
    def _load_settings(self):
        """Load settings from GSettings or create a new one"""
        schema_source = Gio.SettingsSchemaSource.get_default()
        
        if schema_source.lookup('org.example.silktex', True):
            # Use GSettings if the schema is installed
            return Gio.Settings.new('org.example.silktex')
        else:
            # Use in-memory settings for development
            return self._create_memory_settings()
    
    def _create_memory_settings(self):
        """Create an in-memory settings object for development or first run"""
        # Create a dictionary to store settings
        self._memory_settings = {}
        
        # Initialize with defaults
        for key, value in self.defaults.items():
            self._memory_settings[key] = value
        
        return self
    
    def get_boolean(self, key):
        """Get a boolean setting"""
        if isinstance(self.settings, Gio.Settings):
            return self.settings.get_boolean(key)
        else:
            return self._memory_settings.get(key, self.defaults.get(key, False))
    
    def get_int(self, key):
        """Get an integer setting"""
        if isinstance(self.settings, Gio.Settings):
            return self.settings.get_int(key)
        else:
            return self._memory_settings.get(key, self.defaults.get(key, 0))
    
    def get_float(self, key):
        """Get a float setting"""
        if isinstance(self.settings, Gio.Settings):
            return self.settings.get_double(key)
        else:
            return self._memory_settings.get(key, self.defaults.get(key, 0.0))
    
    def get_string(self, key):
        """Get a string setting"""
        if isinstance(self.settings, Gio.Settings):
            return self.settings.get_string(key)
        else:
            return self._memory_settings.get(key, self.defaults.get(key, ''))
    
    def set_boolean(self, key, value):
        """Set a boolean setting"""
        if isinstance(self.settings, Gio.Settings):
            self.settings.set_boolean(key, value)
        else:
            self._memory_settings[key] = value
    
    def set_int(self, key, value):
        """Set an integer setting"""
        if isinstance(self.settings, Gio.Settings):
            self.settings.set_int(key, value)
        else:
            self._memory_settings[key] = value
    
    def set_float(self, key, value):
        """Set a float setting"""
        if isinstance(self.settings, Gio.Settings):
            self.settings.set_double(key, value)
        else:
            self._memory_settings[key] = value
    
    def set_string(self, key, value):
        """Set a string setting"""
        if isinstance(self.settings, Gio.Settings):
            self.settings.set_string(key, value)
        else:
            self._memory_settings[key] = value
    
    def connect(self, key, callback):
        """Connect a callback to a setting change"""
        if isinstance(self.settings, Gio.Settings):
            return self.settings.connect(f'changed::{key}', callback)
        return None
gi.require_version('Gtk', '4.0')
from gi.repository import GLib


class ConfigManager:
    """Manage application configuration"""
    
    def __init__(self):
        """Initialize configuration manager"""
        # Default configuration
        self.defaults = {
            # Window settings
            'window-width': 1200,
            'window-height': 800,
            'is-maximized': False,
            'is-fullscreen': False,
            
            # UI settings
            'sidebar-visible': True,
            'color-scheme': 'system',  # 'light', 'dark', or 'system'
            
            # Editor settings
            'editor-font': 'Monospace 11',
            'editor-show-line-numbers': True,
            'editor-highlight-current-line': True,
            'editor-wrap-text': True,
            'editor-auto-indent': True,
            'editor-tab-width': 4,
            'editor-use-spaces': True,
            'editor-show-right-margin': True,
            'editor-right-margin-position': 80,
            
            # LaTeX settings
            'latex-engine': 'pdflatex',  # pdflatex, xelatex, lualatex
            'latex-shell-escape': False,
            'latex-auto-compile': True,
            
            # Preview settings
            'preview-auto-refresh': True,
            'preview-refresh-delay': 1000,  # milliseconds
            'preview-zoom-level': 1.0
        }
        
        # Load configuration
        self.config = self.defaults.copy()
        self.load()
    
    def get_config_dir(self):
        """Get the configuration directory"""
        config_dir = os.path.join(GLib.get_user_config_dir(), 'silktex')
        
        # Create directory if it doesn't exist
        if not os.path.exists(config_dir):
            os.makedirs(config_dir)
        
        return config_dir
    
    def get_config_file(self):
        """Get the configuration file path"""
        return os.path.join(self.get_config_dir(), 'config.json')
    
    def load(self):
        """Load configuration from file"""
        config_file = self.get_config_file()
        
        if os.path.exists(config_file):
            try:
                with open(config_file, 'r') as f:
                    saved_config = json.load(f)
                
                # Update config with saved values
                for key, value in saved_config.items():
                    if key in self.config:
                        self.config[key] = value
            except Exception as e:
                print(f"Error loading configuration: {e}")
    
    def save(self):
        """Save configuration to file"""
        config_file = self.get_config_file()
        
        try:
            with open(config_file, 'w') as f:
                json.dump(self.config, f, indent=2)
        except Exception as e:
            print(f"Error saving configuration: {e}")
    
    def get_string(self, key):
        """Get a string configuration value"""
        value = self.config.get(key, self.defaults.get(key, ''))
        return str(value)
    
    def set_string(self, key, value):
        """Set a string configuration value"""
        if key in self.config:
            self.config[key] = str(value)
            self.save()
    
    def get_int(self, key):
        """Get an integer configuration value"""
        value = self.config.get(key, self.defaults.get(key, 0))
        return int(value)
    
    def set_int(self, key, value):
        """Set an integer configuration value"""
        if key in self.config:
            self.config[key] = int(value)
            self.save()
    
    def get_boolean(self, key):
        """Get a boolean configuration value"""
        value = self.config.get(key, self.defaults.get(key, False))
        return bool(value)
    
    def set_boolean(self, key, value):
        """Set a boolean configuration value"""
        if key in self.config:
            self.config[key] = bool(value)
            self.save()
    
    def get_float(self, key):
        """Get a float configuration value"""
        value = self.config.get(key, self.defaults.get(key, 0.0))
        return float(value)
    
    def set_float(self, key, value):
        """Set a float configuration value"""
        if key in self.config:
            self.config[key] = float(value)
            self.save()
    
    def reset(self):
        """Reset configuration to defaults"""
        self.config = self.defaults.copy()
        self.save()
# config.py - Application configuration management
import os
import json
from gi.repository import GObject, Gio, GLib
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Configuration module

This module handles application configuration, storing and retrieving
user preferences and settings.
"""

import os
import configparser
import gi

gi.require_version('Gtk', '4.0')
from gi.repository import GLib

class ConfigManager:
    """
    Configuration manager for SilkTex.
    
    This class handles reading and writing application settings, and provides
    default values for settings that are not configured.
    """
    
    DEFAULT_CONFIG = {
        "General": {
            "auto_save": "true",
            "auto_save_interval": "60",
            "recent_files_count": "10",
            "check_updates": "true",
            "default_engine": "pdflatex"
        },
        "Editor": {
            "font": "Monospace 12",
            "line_numbers": "true",
            "wrap_text": "true",
            "auto_indent": "true",
            "highlight_line": "true",
            "tab_width": "4",
            "use_spaces": "true",
            "show_whitespace": "false",
            "style_scheme": "classic"
        },
        "Preview": {
            "auto_update": "true",
            "update_delay": "1000",
            "zoom_level": "1.0",
            "show_toolbar": "true"
        },
        "LaTeX": {
            "additional_path": "",
            "additional_args": "",
            "run_bibtex": "true",
            "run_makeindex": "false"
        },
        "SpellCheck": {
            "enabled": "true",
            "language": "en_US"
        }
    }
    
    def __init__(self):
        """Initialize the configuration manager."""
        self.config = configparser.ConfigParser()
        
        # Load default configuration
        for section, items in self.DEFAULT_CONFIG.items():
            if not self.config.has_section(section):
                self.config.add_section(section)
            for key, value in items.items():
                self.config.set(section, key, value)
        
        # Load user configuration if it exists
        self.config_file = self._get_config_file_path()
        self.load_config()
    
    def _get_config_file_path(self):
        """Get the path to the configuration file.
        
        Returns:
            The path to the configuration file
        """
        config_dir = GLib.get_user_config_dir()
        app_config_dir = os.path.join(config_dir, "silktex")
        
        # Create config directory if it doesn't exist
        os.makedirs(app_config_dir, exist_ok=True)
        
        return os.path.join(app_config_dir, "settings.ini")
    
    def load_config(self):
        """Load configuration from the config file."""
        if os.path.exists(self.config_file):
            try:
                self.config.read(self.config_file)
            except configparser.Error as e:
                print(f"Error loading configuration: {e}")
    
    def save_config(self):
        """Save the current configuration to the config file."""
        try:
            with open(self.config_file, 'w') as f:
                self.config.write(f)
        except Exception as e:
            print(f"Error saving configuration: {e}")
    
    def get_string(self, section, key):
        """Get a string value from the configuration.
        
        Args:
            section: The configuration section
            key: The configuration key
            
        Returns:
            The string value, or the default value if not found
        """
        try:
            return self.config.get(section, key)
        except (configparser.NoSectionError, configparser.NoOptionError):
            if section in self.DEFAULT_CONFIG and key in self.DEFAULT_CONFIG[section]:
                return self.DEFAULT_CONFIG[section][key]
            return ""
    
    def get_boolean(self, section, key):
        """Get a boolean value from the configuration.
        
        Args:
            section: The configuration section
            key: The configuration key
            
        Returns:
            The boolean value, or the default value if not found
        """
        try:
            return self.config.getboolean(section, key)
        except (configparser.NoSectionError, configparser.NoOptionError, ValueError):
            if section in self.DEFAULT_CONFIG and key in self.DEFAULT_CONFIG[section]:
                return self.DEFAULT_CONFIG[section][key].lower() == "true"
            return False
    
    def get_int(self, section, key):
        """Get an integer value from the configuration.
        
        Args:
            section: The configuration section
            key: The configuration key
            
        Returns:
            The integer value, or the default value if not found
        """
        try:
            return self.config.getint(section, key)
        except (configparser.NoSectionError, configparser.NoOptionError, ValueError):
            if section in self.DEFAULT_CONFIG and key in self.DEFAULT_CONFIG[section]:
                try:
                    return int(self.DEFAULT_CONFIG[section][key])
                except ValueError:
                    pass
            return 0
    
    def get_float(self, section, key):
        """Get a float value from the configuration.
        
        Args:
            section: The configuration section
            key: The configuration key
            
        Returns:
            The float value, or the default value if not found
        """
        try:
            return self.config.getfloat(section, key)
        except (configparser.NoSectionError, configparser.NoOptionError, ValueError):
            if section in self.DEFAULT_CONFIG and key in self.DEFAULT_CONFIG[section]:
                try:
                    return float(self.DEFAULT_CONFIG[section][key])
                except ValueError:
                    pass
            return 0.0
    
    def set_value(self, section, key, value):
        """Set a value in the configuration.
        
        Args:
            section: The configuration section
            key: The configuration key
            value: The value to set
        """
        if not self.config.has_section(section):
            self.config.add_section(section)
        
        self.config.set(section, key, str(value))
        self.save_config()

class ConfigManager(GObject.Object):
    """Configuration manager for SilkTex"""
    
    def __init__(self):
        """Initialize the configuration manager"""
        super().__init__()
        
        # Load default configuration
        self.config = self.get_default_config()
        
        # Load user configuration
        self.config_file = os.path.join(GLib.get_user_config_dir(), "silktex", "config.json")
        self.load_config()
    
    def get_default_config(self):
        """Get default configuration values"""
        return {
            # Window settings
            "window-width": 1200,
            "window-height": 800,
            "is-maximized": False,
            "is-fullscreen": False,
            "sidebar-visible": True,
            "editor-preview-split": 600,
            
            # Interface settings
            "color-scheme": "system",  # system, light, dark
            
            # Editor settings
            "editor-font": "Monospace 11",
            "editor-show-line-numbers": True,
            "editor-highlight-current-line": True,
            "editor-show-right-margin": True,
            "editor-right-margin-position": 80,
            "editor-wrap-text": True,
            "editor-auto-indent": True,
            "editor-use-spaces": True,
            "editor-tab-width": 4,
            
            # LaTeX settings
            "latex-engine": "pdflatex",  # pdflatex, xelatex, lualatex
            "latex-shell-escape": False,
            "latex-auto-compile": True,
            
            # Preview settings
            "preview-auto-refresh": True,
            "preview-refresh-delay": 1000,  # milliseconds
            "preview-zoom-level": 1.0,
        }
    
    def load_config(self):
        """Load configuration from the config file"""
        try:
            # Create directory if it doesn't exist
            config_dir = os.path.dirname(self.config_file)
            if not os.path.exists(config_dir):
                os.makedirs(config_dir)
            
            # Load config if the file exists
            if os.path.exists(self.config_file):
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    user_config = json.load(f)
                
                # Update config with user values
                for key, value in user_config.items():
                    self.config[key] = value
        
        except Exception as e:
            print(f"Error loading configuration: {e}")
            # If there was an error, we'll use the default config
    
    def save_config(self):
        """Save configuration to the config file"""
        try:
            # Create directory if it doesn't exist
            config_dir = os.path.dirname(self.config_file)
            if not os.path.exists(config_dir):
                os.makedirs(config_dir)
            
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump(self.config, f, indent=2)
            
            return True
        
        except Exception as e:
            print(f"Error saving configuration: {e}")
            return False
    
    def get_string(self, key):
        """Get a string value from the configuration"""
        return str(self.config.get(key, ""))
    
    def get_int(self, key):
        """Get an integer value from the configuration"""
        value = self.config.get(key, 0)
        try:
            return int(value)
        except (ValueError, TypeError):
            return 0
    
    def get_float(self, key):
        """Get a float value from the configuration"""
        value = self.config.get(key, 0.0)
        try:
            return float(value)
        except (ValueError, TypeError):
            return 0.0
    
    def get_boolean(self, key):
        """Get a boolean value from the configuration"""
        value = self.config.get(key, False)
        return bool(value)
    
    def set_string(self, key, value):
        """Set a string value in the configuration"""
        self.config[key] = str(value)
        self.save_config()
        self.emit(key, value)
    
    def set_int(self, key, value):
        """Set an integer value in the configuration"""
        try:
            self.config[key] = int(value)
            self.save_config()
            self.emit(key, value)
        except (ValueError, TypeError):
            pass
    
    def set_float(self, key, value):
        """Set a float value in the configuration"""
        try:
            self.config[key] = float(value)
            self.save_config()
            self.emit(key, value)
        except (ValueError, TypeError):
            pass
    
    def set_boolean(self, key, value):
        """Set a boolean value in the configuration"""
        self.config[key] = bool(value)
        self.save_config()
        self.emit(key, value)
    
    def connect(self, key, callback):
        """Connect a callback to a configuration key change"""
        signal_id = GObject.signal_lookup(key, ConfigManager)
        if signal_id == 0:
            GObject.signal_new(key, ConfigManager, 
                             GObject.SignalFlags.RUN_LAST,
                             None, (GObject.TYPE_PYOBJECT,))
        
        return super().connect(key, callback)