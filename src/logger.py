import enum
import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gtk

class LogLevel(enum.IntEnum):
    DEBUG = 0
    INFO = 1 
    WARNING = 2
    ERROR = 3
    G_ERROR = 4

# Global parent window for error dialogs
parent_window = None

def slog_set_gui_parent(window):
    """Set parent window for error dialogs"""
    global parent_window
    parent_window = window

def slog(level: LogLevel, message: str):
    """Log a message with the specified level"""
    if level == LogLevel.G_ERROR and parent_window:
        # Show error dialog for GUI errors
        dialog = Gtk.MessageDialog(
            transient_for=parent_window,
            flags=Gtk.DialogFlags.MODAL,
            message_type=Gtk.MessageType.ERROR,
            buttons=Gtk.ButtonsType.OK,
            text=message
        )
        dialog.run()
        dialog.destroy()
    else:
        # Print other messages to console
        prefix = {
            LogLevel.DEBUG: "DEBUG",
            LogLevel.INFO: "INFO",
            LogLevel.WARNING: "WARNING", 
            LogLevel.ERROR: "ERROR",
            LogLevel.G_ERROR: "ERROR"
        }
        print(f"[{prefix[level]}] {message}")
