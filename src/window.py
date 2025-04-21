# window.py

from gi.repository import Gtk, Adw
from .layout import SilktexLayout


class SilktexWindow(Adw.ApplicationWindow):
    def __init__(self, application):
        super().__init__(application=application)
        self.set_default_size(1200, 800)
        self.set_title("SilkTex")

        # 🧩 Dein vollständiges UI statt nur "Hello, World!"
        layout = SilktexLayout()
        self.set_content(layout)  # oder set_child() in GTK4, je nach API-Version

