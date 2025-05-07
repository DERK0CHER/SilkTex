import os
import sys
from gi.repository import Gio, GLib, Gtk
# main.py - Application entry point
import sys
import gi
# main.py - Main application entry point
import sys
import os
import signal
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
gi.require_version('WebKit', '6.0')

from gi.repository import Gtk, Adw, Gio, GLib


def init_logging():
    """Initialize application logging"""
    import logging
    
    # Create logger
    logger = logging.getLogger('silktex')
    logger.setLevel(logging.INFO)
    
    # Create console handler
    console_handler = logging.StreamHandler()
    console_handler.setLevel(logging.INFO)
    
    # Create formatter
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    console_handler.setFormatter(formatter)
    
    # Add handler to logger
    logger.addHandler(console_handler)
    
    return logger


def check_dependencies():
    """Check if all required dependencies are available"""
    dependencies = {
        'Gtk': '4.0',
        'Adw': '1',
        'GtkSource': '5',
        'WebKit': '6.0'
    }
    
    missing = []
    
    for dep_name, dep_version in dependencies.items():
        try:
            gi.require_version(dep_name, dep_version)
        except (ValueError, gi.RepositoryError):
            missing.append(f"{dep_name} {dep_version}")
    
    return missing


def main(version=None):
    """Main entry point for the application
    
    Args:
        version: The application version string
        
    Returns:
        The application exit code
    """
    # Initialize logging
    logger = init_logging()
    logger.info("Starting SilkTex")
    
    # Check dependencies
    missing_deps = check_dependencies()
    if missing_deps:
        logger.error(f"Missing dependencies: {', '.join(missing_deps)}")
        print(f"Error: Missing dependencies: {', '.join(missing_deps)}")
        print("Please install the required dependencies and try again.")
        return 1
    
    # Allow Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    # Set application ID
    app_id = 'org.example.silktex'
    
    # Import here after checking dependencies
    # First try the local import if we're running in development mode
    try:
        from .window import SilkTexWindow
        from .config import ConfigManager
        
        # Create our own application class since we can't import the main one
        class SilkTexApp(Adw.Application):
            def __init__(self, application_id):
                super().__init__(application_id=application_id,
                                 flags=Gio.ApplicationFlags.FLAGS_NONE)
                self.window = None
                
            def do_activate(self):
                self.window = SilkTexWindow(application=self)
                self.window.present()
    except ImportError:
        # If local import fails, try the installed package
        try:
            from silktex import SilkTexApp
        except ImportError:
            print("Could not import SilkTexApp. Creating a minimal fallback application.")
            
            # Create minimal fallback application if everything else fails
            class SilkTexApp(Adw.Application):
                def __init__(self, application_id):
                    super().__init__(application_id=application_id,
                                     flags=Gio.ApplicationFlags.FLAGS_NONE)
                    self.version = "unknown"
                    
                def do_activate(self):
                    window = Gtk.ApplicationWindow(application=self, title="SilkTex")
                    window.set_default_size(800, 600)
                    
                    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
                    box.set_margin_top(50)
                    box.set_margin_bottom(50)
                    box.set_margin_start(50)
                    box.set_margin_end(50)
                    
                    label = Gtk.Label()
                    label.set_markup("<span size='xx-large'>SilkTex</span>")
                    box.append(label)
                    
                    error_label = Gtk.Label()
                    error_label.set_markup(
                        "<span foreground='red'>Error: Could not import required modules</span>\n\n"
                        "Python path:\n" + "\n".join(sys.path)
                    )
                    error_label.set_wrap(True)
                    box.append(error_label)
                    
                    window.set_child(box)
                    window.present()
    
    try:
        # Create the application
        app = SilkTexApp(application_id=app_id)
        
        # Set program name
        GLib.set_application_name('SilkTex')
        
        # Configure resource path if running installed version
        resource_path = os.path.join(sys.prefix, 'share', 'silktex', 'silktex.gresource')
        local_resource_path = os.path.join(os.path.dirname(__file__), '..', 'data', 'resources', 'silktex.gresource')
        
        # Try to load resources from various locations
        resource_locations = [
            resource_path,
            local_resource_path,
            'silktex.gresource',
            os.path.join('data', 'resources', 'silktex.gresource')
        ]
        
        resource_loaded = False
        for path in resource_locations:
            if os.path.exists(path):
                try:
                    logger.info(f"Loading resources from {path}")
                    resource = Gio.Resource.load(path)
                    Gio.resources_register(resource)
                    resource_loaded = True
                    break
                except Exception as e:
                    logger.warning(f"Failed to load resource from {path}: {e}")
        
        if not resource_loaded:
            logger.warning("No resources loaded, UI may not display correctly")
        
        # Set version if provided
        if version:
            app.version = version
        
        # Run the application
        return app.run(sys.argv)
    
    except Exception as e:
        logger.error(f"Error during application startup: {e}", exc_info=True)
        print(f"Error during application startup: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
gi.require_version('WebKit', '6.0')

from gi.repository import Gtk, Gio, Adw

from silktex.window import SilkTexWindow
from silktex.config import ConfigManager

it #!/usr/bin/env python3
# main.py - Application entry point

import sys
import os
import signal
import logging
import gi

# Set up required GObject Introspection libraries
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
try:
    gi.require_version('WebKit', '6.0')
except ValueError:
    # WebKit is recommended but technically optional
    pass

from gi.repository import Gtk, Adw, Gio, GLib

# Initialize Adw
Adw.init()

# Initialize logging
logger = logging.getLogger('silktex')
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.INFO)

class SilkTexWindow(Adw.ApplicationWindow):
    """Main window for SilkTex application"""
    
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        
        # Basic window setup
        self.set_default_size(1000, 700)
        self.set_title("SilkTex")
        
        # Create a toast overlay and a box for content
        self.toast_overlay = Adw.ToastOverlay()
        self.set_content(self.toast_overlay)
        
        self.main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.toast_overlay.set_child(self.main_box)
        
        # Create a header bar
        self.header = Adw.HeaderBar()
        self.main_box.append(self.header)
        
        # Add a menu button
        menu = Gio.Menu.new()
        menu.append("Preferences", "app.preferences")
        menu.append("About", "app.about")
        menu.append("Quit", "app.quit")
        
        menu_button = Gtk.MenuButton()
        menu_button.set_icon_name("open-menu-symbolic")
        menu_button.set_menu_model(menu)
        self.header.pack_end(menu_button)
        
        # Main content area
        self.welcome_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.welcome_box.set_margin_top(50)
        self.welcome_box.set_margin_bottom(50)
        self.welcome_box.set_margin_start(50)
        self.welcome_box.set_margin_end(50)
        self.welcome_box.set_vexpand(True)
        self.welcome_box.set_hexpand(True)
        self.welcome_box.set_valign(Gtk.Align.CENTER)
        self.welcome_box.set_halign(Gtk.Align.CENTER)
        
        # Add a logo or title
        title = Gtk.Label()
        title.set_markup("<span size='xx-large' weight='bold'>SilkTex</span>")
        self.welcome_box.append(title)
        
        subtitle = Gtk.Label()
        subtitle.set_markup("<span size='large'>LaTeX Editor with Live Preview</span>")
        self.welcome_box.append(subtitle)
        
        # Add some space
        spacer = Gtk.Box()
        spacer.set_size_request(-1, 20)
        self.welcome_box.append(spacer)
        
        # Add buttons for common actions
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_halign(Gtk.Align.CENTER)
        
        new_button = Gtk.Button(label="New Document")
        new_button.set_action_name("app.new")
        button_box.append(new_button)
        
        open_button = Gtk.Button(label="Open Document")
        open_button.set_action_name("app.open")
        button_box.append(open_button)
        
        self.welcome_box.append(button_box)
        
        # Add the welcome box to the main box
        self.main_box.append(self.welcome_box)
    
    def show_preferences(self):
        """Show preferences dialog"""
        toast = Adw.Toast(title="Preferences not implemented yet")
        self.toast_overlay.add_toast(toast)
    
    def new_document(self):
        """Create a new document"""
        toast = Adw.Toast(title="New document feature not implemented yet")
        self.toast_overlay.add_toast(toast)
    
    def open_document(self):
        """Open a document"""
        dialog = Gtk.FileDialog()
        dialog.set_title("Open LaTeX Document")
        
        filters = Gio.ListStore.new(Gtk.FileFilter)
        
        filter_tex = Gtk.FileFilter()
        filter_tex.set_name("LaTeX Files")
        filter_tex.add_pattern("*.tex")
        filters.append(filter_tex)
        
        filter_all = Gtk.FileFilter()
        filter_all.set_name("All Files")
        filter_all.add_pattern("*")
        filters.append(filter_all)
        
        dialog.set_filters(filters)
        
        # Use a lambda to capture self
        dialog.open(self, None, lambda dialog, result: self.on_open_dialog_response(dialog, result))
    
    def on_open_dialog_response(self, dialog, result):
        """Handle file open dialog response"""
        try:
            file = dialog.open_finish(result)
            if file:
                toast = Adw.Toast(title=f"Opening: {file.get_path()}")
                self.toast_overlay.add_toast(toast)
        except GLib.Error as error:
            print(f"Error opening file: {error.message}")
    
    def save_document(self):
        """Save the current document"""
        toast = Adw.Toast(title="Save feature not implemented yet")
        self.toast_overlay.add_toast(toast)
    
    def save_document_as(self):
        """Save the current document as a new file"""
        toast = Adw.Toast(title="Save As feature not implemented yet")
        self.toast_overlay.add_toast(toast)
    
    def compile_document(self):
        """Compile the current document"""
        toast = Adw.Toast(title="Compile feature not implemented yet")
        self.toast_overlay.add_toast(toast)


class SilkTexApplication(Adw.Application):
    """Main application class for SilkTex"""
    
    def __init__(self, version=None):
        super().__init__(application_id='org.example.silktex',
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        
        self.version = version or "0.1.0"
        
        # Set up actions
        self.create_action('quit', self.on_quit_action, ['<primary>q'])
        self.create_action('about', self.on_about_action)
        self.create_action('preferences', self.on_preferences_action, ['<primary>comma'])
        self.create_action('new', self.on_new_action, ['<primary>n'])
        self.create_action('open', self.on_open_action, ['<primary>o'])
        self.create_action('save', self.on_save_action, ['<primary>s'])
        self.create_action('save-as', self.on_save_as_action, ['<primary><shift>s'])
        self.create_action('compile', self.on_compile_action, ['F5'])
        
    def do_activate(self):
        """Handle application activation"""
        # Get the active window or create a new one
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self)
        
        # Present the window
        win.present()
    
    def on_quit_action(self, widget, _):
        """Handle quit action"""
        self.quit()
    
    def on_about_action(self, widget, _):
        """Show about dialog"""
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                           application_name='SilkTex',
                           application_icon='org.example.silktex',
                           developer_name='SilkTex Developers',
                           version=self.version,
                           developers=['SilkTex Team'],
                           copyright='© 2023 SilkTex Team',
                           license_type=Gtk.License.GPL_3_0)
        about.present()
    
    def on_preferences_action(self, widget, _):
        """Show preferences dialog"""
        window = self.props.active_window
        if window:
            window.show_preferences()
    
    def on_new_action(self, widget, _):
        """Create a new document"""
        window = self.props.active_window
        if window:
            window.new_document()
    
    def on_open_action(self, widget, _):
        """Open a document"""
        window = self.props.active_window
        if window:
            window.open_document()
    
    def on_save_action(self, widget, _):
        """Save the current document"""
        window = self.props.active_window
        if window:
            window.save_document()
    
    def on_save_as_action(self, widget, _):
        """Save the current document as a new file"""
        window = self.props.active_window
        if window:
            window.save_document_as()
    
    def on_compile_action(self, widget, _):
        """Compile the current document"""
        window = self.props.active_window
        if window:
            window.compile_document()
    
    def create_action(self, name, callback, shortcuts=None):
        """Helper to create an action"""
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)


def main(version=None):
    """Main entry point for the application
    
    Args:
        version: The application version string
        
    Returns:
        The application exit code
    """
    # Initialize logging
    logger.info("Starting SilkTex")
    
    # Allow Ctrl+C to work properly
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    
    try:
        # Create and run the application
        app = SilkTexApplication(version=version)
        
        # Set program name
        GLib.set_application_name('SilkTex')
        
        # Try to find resources
        resource_path = os.path.join(sys.prefix, 'share', 'silktex', 'silktex.gresource')
        local_resource_path = os.path.join(os.path.dirname(__file__), '..', 'data', 'resources', 'silktex.gresource')
        
        # Try to load resources from various locations
        resource_locations = [
            resource_path,
            local_resource_path,
            'silktex.gresource',
            os.path.join('data', 'resources', 'silktex.gresource')
        ]
        
        resource_loaded = False
        for path in resource_locations:
            if os.path.exists(path):
                try:
                    logger.info(f"Loading resources from {path}")
                    resource = Gio.Resource.load(path)
                    Gio.resources_register(resource)
                    resource_loaded = True
                    break
                except Exception as e:
                    logger.warning(f"Failed to load resource from {path}: {e}")
        
        if not resource_loaded:
            logger.warning("No resources loaded, UI may not display correctly")
        
        # Run the application
        return app.run(sys.argv)
    
    except Exception as e:
        logger.error(f"Error during application startup: {e}", exc_info=True)
        print(f"Error during application startup: {e}")
        return 1


if __name__ == '__main__':
    sys.exit(main())


def main(version):
    """Application main entry point"""
    app = SilkTexApplication(version)
    return app.run(sys.argv)


if __name__ == '__main__':
    main('development')
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
#!/usr/bin/env python3
# main.py - Main application entry point
import sys
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
gi.require_version('WebKit', '6.0')

from gi.repository import Gtk, Gio, Adw, GLib

from silktex.window import SilkTexWindow
from silktex.config import ConfigManager

class SilkTexApplication(Adw.Application):
    """Main SilkTex application class"""
    
    def __init__(self):
        super().__init__(application_id='org.example.silktex',
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        
        # Initialize application actions
        self.create_action('quit', self.on_quit_action, ['<primary>q'])
        self.create_action('new', self.on_new_action, ['<primary>n'])
        self.create_action('open', self.on_open_action, ['<primary>o'])
        self.create_action('save', self.on_save_action, ['<primary>s'])
        self.create_action('save-as', self.on_save_as_action, ['<primary><shift>s'])
        self.create_action('about', self.on_about_action)
        self.create_action('preferences', self.on_preferences_action, ['<primary>comma'])
        
        # Load configuration
        self.config = ConfigManager()
        
        # Set application styles
        self.style_manager = Adw.StyleManager.get_default()
        color_scheme = self.config.get_string('color-scheme')
        if color_scheme == 'light':
            self.style_manager.set_color_scheme(Adw.ColorScheme.FORCE_LIGHT)
        elif color_scheme == 'dark':
            self.style_manager.set_color_scheme(Adw.ColorScheme.FORCE_DARK)
        else:
            self.style_manager.set_color_scheme(Adw.ColorScheme.DEFAULT)

    def do_activate(self):
        """Handle application activation"""
        # Get the active window or create one
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self)
        win.present()
    
    def on_quit_action(self, widget, _):
        """Quit the application"""
        self.quit()
    
    def on_new_action(self, widget, _):
        """Create a new document"""
        win = self.props.active_window
        if win:
            win.new_document()
    
    def on_open_action(self, widget, _):
        """Open a document"""
        win = self.props.active_window
        if win:
            win.open_document()
    
    def on_save_action(self, widget, _):
        """Save the current document"""
        win = self.props.active_window
        if win:
            win.save_document()
    
    def on_save_as_action(self, widget, _):
        """Save the current document with a new name"""
        win = self.props.active_window
        if win:
            win.save_document_as()
    
    def on_about_action(self, widget, _):
        """Show the about dialog"""
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                               application_name='SilkTex',
                               application_icon='org.example.silktex',
                               developer_name='SilkTex Team',
                               version='0.1.0',
                               developers=['SilkTex Team'],
                               copyright='© 2023 SilkTex Team',
                               license_type=Gtk.License.MIT_X11,
                               website='https://github.com/yourusername/silktex',
                               issue_url='https://github.com/yourusername/silktex/issues')
        about.present()
    
    def on_preferences_action(self, widget, _):
        """Show the preferences dialog"""
        win = self.props.active_window
        if win:
            win.show_preferences()
    
    def create_action(self, name, callback, shortcuts=None):
        """Create a GAction and add it to the application"""
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)


def main(version=None):
    """Main entry point for the application"""
    app = SilkTexApplication()
    return app.run(sys.argv)


if __name__ == "__main__":
    main()
import sys
import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, Gio, Adw, GLib

# Try to import from the current directory first
try:
    import silktex
except ImportError:
    # If that fails, add the current directory to the path and try again
    current_dir = os.path.dirname(os.path.abspath(__file__))
    if current_dir not in sys.path:
        sys.path.insert(0, current_dir)
    import silktex

def main(version):
    app = silktex.SilkTexApplication()
    return app.run(sys.argv)

if __name__ == '__main__':
    main(None)
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