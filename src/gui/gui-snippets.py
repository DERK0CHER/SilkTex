import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource, Gdk, GObject
import os
from dataclasses import dataclass
from typing import Optional, List, Tuple

# Constants
PACKAGE = "gummi"
GUMMI_DATA = "/usr/share/gummi"  # This should be configured appropriately

@dataclass
class Tuple2:
    first: any
    second: any

class SList:
    def __init__(self, first=None, second=None, next=None):
        self.first = first
        self.second = second
        self.next = next

class GuSnippetsGui:
    def __init__(self, main_window: Gtk.Window):
        self.builder = Gtk.Builder()
        ui_file = os.path.join(GUMMI_DATA, "ui", "snippets.ui")
        self.builder.add_from_file(ui_file)
        self.builder.set_translation_domain(PACKAGE)

        # Get widgets from builder
        self.snippets_window = self.builder.get_object("snippetswindow")
        self.snippets_tree_view = self.builder.get_object("snippets_tree_view")
        self.snippet_scroll = self.builder.get_object("snippet_scroll")
        self.tab_trigger_entry = self.builder.get_object("tab_trigger_entry")
        self.accelerator_entry = self.builder.get_object("accelerator_entry")
        self.list_snippets = self.builder.get_object("list_snippets")
        self.snippet_renderer = self.builder.get_object("snippet_renderer")
        self.button_new = self.builder.get_object("button_new_snippet")
        self.button_remove = self.builder.get_object("button_remove_snippet")

        # Initialize GtkSourceView
        manager = GtkSource.LanguageManager.get_default()
        lang_dir = os.path.join(GUMMI_DATA, "snippets")
        search_paths = list(manager.get_search_path())
        search_paths.append(lang_dir)
        manager.set_search_path(search_paths)
        
        lang = manager.get_language("snippets")
        self.buffer = GtkSource.Buffer.new_with_language(lang)
        self.view = GtkSource.View.new_with_buffer(self.buffer)
        self.snippet_scroll.set_child(self.view)

        # Initialize other properties
        self.current = None
        
        # Load snippets
        self.load_snippets()

        # Connect signals
        self.view.connect("key-release-event", self.on_snippet_source_buffer_key_release)
        self.connect_signals()

        # Set window parent
        self.snippets_window.set_transient_for(main_window)

    def connect_signals(self):
        """Connect all GUI signals"""
        self.builder.connect_signals({
            "on_snippetsgui_close_clicked": self.on_close_clicked,
            "on_snippetsgui_reset_clicked": self.on_reset_clicked,
            "on_snippetsgui_selected_text_clicked": self.on_selected_text_clicked,
            "on_snippetsgui_filename_clicked": self.on_filename_clicked,
            "on_snippetsgui_basename_clicked": self.on_basename_clicked,
            "on_button_new_snippet_clicked": self.on_new_snippet_clicked,
            "on_button_remove_snippet_clicked": self.on_remove_snippet_clicked,
            "on_tab_trigger_entry_key_release_event": self.on_tab_trigger_key_release,
            "on_accelerator_entry_focus_in_event": self.on_accelerator_focus_in,
            "on_accelerator_entry_focus_out_event": self.on_accelerator_focus_out,
            "on_accelerator_entry_key_press_event": self.on_accelerator_key_press,
            "on_snippets_tree_view_cursor_changed": self.on_tree_cursor_changed,
            "on_snippet_renderer_edited": self.on_snippet_renderer_edited,
            "on_snippet_renderer_editing_canceled": self.on_snippet_renderer_editing_canceled
        })

    def show(self):
        """Show the snippets window"""
        self.snippets_window.show()

    def insert_at_current(self, text: str):
        """Insert text at current cursor position"""
        mark = self.buffer.get_insert()
        iter = self.buffer.get_iter_at_mark(mark)
        self.buffer.insert(iter, text, -1)

    def load_snippets(self):
        """Load snippets into the tree view"""
        current = self.gummi.snippets.head
        self.list_snippets.clear()
        
        while current:
            if current.second:
                configs = current.first.split(",")
                iter = self.list_snippets.append()
                self.list_snippets.set(iter,
                    [0, 1, 2],
                    [configs[2], configs[0], configs[1]]
                )
            current = current.next

    def move_cursor_to_row(self, row: int):
        """Move cursor to specific row in tree view"""
        row = max(0, row)
        path = Gtk.TreePath.new_from_string(str(row))
        column = self.snippets_tree_view.get_column(0)
        self.snippets_tree_view.set_cursor(path, column, False)

    def update_snippet(self, snippets):
        """Update the current snippet"""
        new_key = self.tab_trigger_entry.get_text()
        new_accel = self.accelerator_entry.get_text()
        
        configs = self.current.first.split(",")
        
        selection = self.snippets_tree_view.get_selection()
        model, iter = selection.get_selected()
        
        self.current.first = f"{new_key},{new_accel},{configs[2]}"
        self.list_snippets.set(iter,
            [0, 1, 2],
            [configs[2], new_key, new_accel]
        )

        # Handle accelerator updates
        snippets.accel_disconnect(configs[0])
        
        if new_accel:
            keyval, mod = Gtk.accelerator_parse(new_accel)
            data = Tuple2(snippets, new_key)
            closure = GObject.Closure(self.snippets_accel_cb, data)
            closure_data = Tuple2(new_key, closure)
            snippets.closure_data.append(closure_data)
            snippets.accel_connect(keyval, mod, closure)

    # Signal Handlers
    def on_close_clicked(self, widget):
        self.snippets_window.hide()
        self.gummi.snippets.save()

    def on_reset_clicked(self, widget):
        self.gummi.snippets.set_default()
        self.load_snippets()
        self.move_cursor_to_row(0)

    def on_selected_text_clicked(self, widget):
        self.insert_at_current("$SELECTED_TEXT")

    def on_filename_clicked(self, widget):
        self.insert_at_current("$FILENAME")

    def on_basename_clicked(self, widget):
        self.insert_at_current("$BASENAME")

    def on_new_snippet_clicked(self, widget):
        iter = self.list_snippets.append()
        self.snippet_renderer.set_property("editable", True)
        
        column = self.snippets_tree_view.get_column(0)
        path = self.list_snippets.get_path(iter)
        
        self.button_new.set_sensitive(False)
        self.button_remove.set_sensitive(False)
        
        self.snippets_tree_view.set_cursor(path, column, True)

    def on_remove_snippet_clicked(self, widget, user_data=None):
        selection = self.snippets_tree_view.get_selection()
        model, iter = selection.get_selected()
        
        if iter:
            name, key, accel = model.get(iter, 0, 1, 2)
            path = model.get_path(iter)
            path_str = path.to_string()
            
            if widget:  # Called directly, not from renderer
                config = f"{key},{accel},{name}"
                target = self.gummi.snippets.find(config, False, False)
                self.gummi.snippets.remove(target)
                
            if key:
                self.gummi.snippets.accel_disconnect(key)
                
            if self.list_snippets.remove(iter):
                self.move_cursor_to_row(int(path_str))
            elif self.list_snippets.get_iter_first()[1]:
                self.move_cursor_to_row(int(path_str) - 1)
            else:
                self.buffer.set_text("", 0)
                self.tab_trigger_entry.set_text("")
                self.accelerator_entry.set_text("")

    def on_tab_trigger_key_release(self, entry, event):
        new_key = entry.get_text()
        search_key = f"{new_key},"
        index = self.gummi.snippets.find(search_key, True, False)
        
        if index and index != self.current:
            entry.set_text("")
            print("Duplicate activation tab trigger detected! Please choose another one.")
        else:
            self.update_snippet(self.gummi.snippets)
        return False

    def on_accelerator_focus_in(self, widget, event):
        if not self.accelerator_entry.get_text():
            self.accelerator_entry.set_text("Type a new shortcut")
        else:
            self.accelerator_entry.set_text(
                "Type a new shortcut, or press Backspace to clear")

    def on_accelerator_focus_out(self, widget, event):
        configs = self.current.first.split(",")
        self.accelerator_entry.set_text(configs[1])

    def on_accelerator_key_press(self, widget, event):
        if event.keyval == Gdk.KEY_Escape:
            self.accelerator_entry.set_text("")
            self.update_snippet(self.gummi.snippets)
            self.snippets_tree_view.grab_focus()
        elif event.keyval in (Gdk.KEY_BackSpace, Gdk.KEY_Delete):
            self.accelerator_entry.set_text("")
            self.update_snippet(self.gummi.snippets)
            self.snippets_tree_view.grab_focus()
        elif Gtk.accelerator_valid(event.keyval, event.state):
            new_accel = Gtk.accelerator_name(
                event.keyval,
                Gdk.ModifierType.get_default_mod_mask() & event.state
            )
            self.accelerator_entry.set_text(new_accel)
            self.update_snippet(self.gummi.snippets)
            self.snippets_tree_view.grab_focus()
        return True

    def on_tree_cursor_changed(self, view, user_data=None):
        selection = view.get_selection()
        model, iter = selection.get_selected()
        
        if iter:
            name, key, accel = model.get(iter, 0, 1, 2)
            
            if not any((name, key, accel)):
                return
                
            config = f"{key},{accel},{name}"
            self.current = self.gummi.snippets.find(config, False, False)
            
            snippet = self.gummi.snippets.get_value(key)
            
            self.buffer.set_text(snippet, -1)
            self.tab_trigger_entry.set_text(key)
            self.accelerator_entry.set_text(accel)

    def on_snippet_renderer_edited(self, renderer, path, name):
        renderer.set_property("editable", False)
        selection = self.snippets_tree_view.get_selection()
        model, iter = selection.get_selected()
        
        if iter:
            self.list_snippets.set(iter, [0, 1, 2], [name, "", ""])
            if name:
                node = SList(f",,{name}", "")
                self.gummi.snippets.append(node)
                self.current = node
                self.on_tree_cursor_changed(self.snippets_tree_view)
            else:
                self.on_remove_snippet_clicked(None)
                
        self.button_new.set_sensitive(True)
        self.button_remove.set_sensitive(True)

    def on_snippet_renderer_editing_canceled(self, renderer):
        self.on_snippet_renderer_edited(renderer, "", "")

    def on_snippet_source_buffer_key_release(self, widget, event):
        start, end = self.buffer.get_bounds()
        text = self.buffer.get_text(start, end, False)
        self.current.second = text
        return False

def create_snippets_gui(main_window):
    """Create and return a new snippets GUI instance"""
    return GuSnippetsGui(main_window)
 