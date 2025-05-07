import sys
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, Gio, Adw, GLib


class SilkTexWindow(Adw.ApplicationWindow):
    __gtype_name__ = 'SilkTexWindow'

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.builder = Gtk.Builder.new_from_resource('/org/example/silktex/main.ui')
        self.set_content(self.builder.get_object('content'))
        
        # Get references to widgets we need to interact with
        self.toast_overlay = self.builder.get_object('toast_overlay')
        self.main_stack = self.builder.get_object('main_stack')
        self.get_started_button = self.builder.get_object('get_started_button')
        self.dark_mode_switch = self.builder.get_object('dark_mode_switch')
        
        # Connect signals
        self.get_started_button.connect('clicked', self.on_get_started_clicked)
        self.dark_mode_switch.connect('state-set', self.on_dark_mode_toggled)
    
    def on_get_started_clicked(self, button):
        self.main_stack.set_visible_child_name('dashboard')
        toast = Adw.Toast.new(_("Welcome to the SilkTex Dashboard!"))
        toast.set_timeout(3)
        self.toast_overlay.add_toast(toast)
    
    def on_dark_mode_toggled(self, switch, state):
        style_manager = Adw.StyleManager.get_default()
        if state:
            style_manager.set_color_scheme(Adw.ColorScheme.FORCE_DARK)
        else:
            style_manager.set_color_scheme(Adw.ColorScheme.FORCE_LIGHT)
        return False


class SilkTexApplication(Adw.Application):
    def __init__(self):
        super().__init__(application_id='org.example.silktex',
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        self.create_action('quit', self.quit, ['<primary>q'])
        self.create_action('about', self.on_about_action)
        self.create_action('preferences', self.on_preferences_action)

    def do_activate(self):
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self)
        win.present()

    def on_about_action(self, widget, _):
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                                application_name='SilkTex',
                                application_icon='org.example.silktex',
                                developer_name='Developer',
                                version='0.1.0',
                                developers=['Your Name'],
                                copyright='© 2023 Your Name')
        about.present()

    def on_preferences_action(self, widget, _):
        self.props.active_window.main_stack.set_visible_child_name('settings')

    def create_action(self, name, callback, shortcuts=None):
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)
