import gi
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, Gdk, GLib
import tempfile
import os
import subprocess

# Import settings
from .settings import settings, apply_editor_settings, get_latex_command

class SilktexLayout(Gtk.Box):
    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        self.set_spacing(0)

        # Temp directory for LaTeX rendering
        self.temp_dir = tempfile.mkdtemp(prefix="silktex_")

        # Headerbar
        headerbar = Adw.HeaderBar()
        headerbar.set_title_widget(Gtk.Label(label="SilkTex"))
        settings_button = Gtk.Button.new_with_label("⚙")
        settings_button.connect("clicked", self.on_settings_clicked)
        headerbar.pack_end(settings_button)

        # Editor + Vorschau
        editor_buffer = Gtk.TextBuffer()
        self.editor = Gtk.TextView.new_with_buffer(editor_buffer)

        # Apply settings to editor
        apply_editor_settings(self.editor)

        # Scrolled window for editor
        editor_scroll = Gtk.ScrolledWindow()
        editor_scroll.set_child(self.editor)

        # Fake-Placeholder: Overlay
        placeholder = Gtk.Label(label="📝 Schreibe hier LaTeX-Code...")
        placeholder.set_halign(Gtk.Align.START)
        placeholder.set_valign(Gtk.Align.START)
        placeholder.set_margin_top(16)
        placeholder.set_margin_start(16)
        placeholder.set_sensitive(False)

        overlay = Gtk.Overlay()
        overlay.set_child(editor_scroll)
        overlay.add_overlay(placeholder)

        # Simple preview for now
        self.preview_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.preview_label = Gtk.Label(label="📄 Vorschau wird hier angezeigt")
        self.preview_label.set_vexpand(True)
        self.preview_label.set_hexpand(True)
        self.preview_box.append(self.preview_label)

        # Connect buffer change signal
        def on_buffer_changed(buffer):
            text = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
            placeholder.set_visible(text.strip() == "")

            # Auto-update preview if enabled
            if settings.get_boolean("Preview", "auto_preview"):
                if hasattr(self, '_update_timeout') and self._update_timeout:
                    GLib.source_remove(self._update_timeout)
                delay = settings.get_integer("Preview", "preview_delay")
                self._update_timeout = GLib.timeout_add(delay, self.update_preview, text)

        editor_buffer.connect("changed", on_buffer_changed)

        # Split view
        self.paned = Gtk.Paned.new(Gtk.Orientation.HORIZONTAL)
        self.paned.set_start_child(overlay)
        self.paned.set_end_child(self.preview_box)
        self.paned.set_resize_start_child(True)
        self.paned.set_position(settings.get_integer("Interface", "paned_position"))

        # Bottombar
        self.status_label = Gtk.Label(label="Status: bereit")
        bottom_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        bottom_bar.set_margin_start(6)
        bottom_bar.set_margin_end(6)
        bottom_bar.set_margin_top(6)
        bottom_bar.set_margin_bottom(6)
        bottom_bar.append(self.status_label)

        # Aufbau
        self.append(headerbar)
        self.append(self.paned)
        self.append(bottom_bar)

        # Connect signals for saving state
        self.connect("destroy", self.on_destroy)

    def on_settings_clicked(self, button):
        # Here you would open a settings dialog
        # For now, just update the status
        self.status_label.set_text("Status: Einstellungen (noch nicht implementiert)")

    def on_destroy(self, widget):
        # Save window state before closing
        self.save_layout_state()

    def save_layout_state(self):
        # Save paned position
        settings.set_integer("Interface", "paned_position", self.paned.get_position())
        settings.save()

    def update_preview(self, latex_code=None):
        # If no code provided, get it from the editor
        if latex_code is None:
            buffer = self.editor.get_buffer()
            start, end = buffer.get_bounds()
            latex_code = buffer.get_text(start, end, False)

        if not latex_code.strip():
            self.preview_label.set_text("📄 Vorschau wird hier angezeigt")
            self.status_label.set_text("Status: Kein LaTeX-Code eingegeben")
            return False

        # Update status
        self.status_label.set_text("Status: Generiere LaTeX-Vorschau...")

        # For now, just show the LaTeX code in the preview
        # In a real implementation, this would compile LaTeX and show the result
        self.preview_label.set_text(f"LaTeX-Code:\n\n{latex_code}")
        self.status_label.set_text("Status: Vorschau aktualisiert")

        # Later you can implement actual LaTeX compilation:
        # self.compile_latex(latex_code)

        self._update_timeout = None
        return False  # Don't repeat

    def compile_latex(self, latex_code):
        """Compile LaTeX code to PDF (to be implemented)"""
        # Create temporary files
        tex_file = os.path.join(self.temp_dir, "preview.tex")

        # Write LaTeX code to temporary file
        with open(tex_file, "w") as f:
            f.write(latex_code)

        # Compile LaTeX to PDF
        try:
            # Get command from settings
            command = get_latex_command()

            subprocess.run(
                [command, "-interaction=nonstopmode", "-output-directory", self.temp_dir, tex_file],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=5
            )

            pdf_file = os.path.join(self.temp_dir, "preview.pdf")

            # Check if PDF was created
            if os.path.exists(pdf_file):
                # Here you would display the PDF
                self.status_label.set_text("Status: PDF generiert")
            else:
                self.status_label.set_text("Status: Fehler beim Generieren des PDFs")

        except Exception as e:
            self.status_label.set_text(f"Status: Fehler - {str(e)}")
