# project_sidebar.py - Project sidebar for file navigation
import os
import gi
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Project Sidebar

This module provides a project sidebar for SilkTex, displaying files
in the current project directory and allowing navigation.
"""

import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Gdk, GObject, Gio, GLib

class ProjectSidebar(Gtk.Box):
    """
    Project sidebar widget for displaying project files.
    
    This widget shows a tree view of files in the current project
    directory, with support for filtering, navigation, and file operations.
    """
    
    __gsignals__ = {
        'file-activated': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
    }
    
    def __init__(self, window):
        """Initialize the project sidebar.
        
        Args:
            window: The main application window
        """
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        self.window = window
        self.project_directory = None
        
        # Create UI components
        self.create_toolbar()
        self.create_file_tree()
        self.create_context_menu()
        self.setup_drag_and_drop()
        
        # Set initial directory label
        self.directory_label.set_text("No Project")
        
    def create_toolbar(self):
        """Create the toolbar for the project sidebar."""
        # Create toolbar
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        toolbar.set_spacing(6)
        toolbar.set_margin_start(6)
        toolbar.set_margin_end(6)
        toolbar.set_margin_top(6)
        toolbar.set_margin_bottom(6)
        
        # Project label
        self.directory_label = Gtk.Label()
        self.directory_label.set_ellipsize(3)  # PANGO_ELLIPSIZE_END
        self.directory_label.set_xalign(0)
        self.directory_label.set_hexpand(True)
        toolbar.append(self.directory_label)
        
        # Refresh button
        refresh_button = Gtk.Button.new_from_icon_name("view-refresh-symbolic")
        refresh_button.set_tooltip_text("Refresh Files")
        refresh_button.connect("clicked", self.on_refresh_clicked)
        toolbar.append(refresh_button)
        
        # Add toolbar to sidebar
        self.append(toolbar)
        
        # Add separator
        separator = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        self.append(separator)
        
    def create_file_tree(self):
        """Create the file tree view for project files."""
        # Create tree model
        # Columns: name, path, icon name, is filtered out, is directory
        self.file_store = Gtk.TreeStore(str, str, str, bool, bool)
        
        # Create tree view
        self.file_tree = Gtk.TreeView()
        self.file_tree.set_model(self.file_store)
        self.file_tree.set_headers_visible(False)
        
        # Create renderer for file icons and names
        renderer = Gtk.CellRendererPixbuf()
        column = Gtk.TreeViewColumn("Files")
        column.pack_start(renderer, False)
        column.set_cell_data_func(renderer, self._icon_data_func)
        
        # Create text renderer
        text_renderer = Gtk.CellRendererText()
        column.pack_start(text_renderer, True)
        column.add_attribute(text_renderer, "text", 0)
        
        # Add column to tree view
        self.file_tree.append_column(column)
        
        # Set up selection handling
        select = self.file_tree.get_selection()
        select.set_mode(Gtk.SelectionMode.SINGLE)
        
        # Connect signals
        self.file_tree.connect("row-activated", self.on_row_activated)
        self.file_tree.connect("button-press-event", self.on_right_click)
        
        # Create a scrolled window for the tree view
        scrolled_window = Gtk.ScrolledWindow()
        scrolled_window.set_hexpand(True)
        scrolled_window.set_vexpand(True)
        scrolled_window.set_child(self.file_tree)
        
        self.append(scrolled_window)
        
        # Add a search filter entry at the bottom
        filter_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        filter_box.set_spacing(6)
        filter_box.set_margin_start(6)
        filter_box.set_margin_end(6)
        filter_box.set_margin_top(6)
        filter_box.set_margin_bottom(6)
        
        search_icon = Gtk.Image.new_from_icon_name("system-search-symbolic")
        filter_box.append(search_icon)
        
        self.filter_entry = Gtk.Entry()
        self.filter_entry.set_hexpand(True)
        self.filter_entry.set_placeholder_text("Filter Files")
        self.filter_entry.connect("changed", self.on_filter_changed)
        
        filter_box.append(self.filter_entry)
        
        self.append(filter_box)
        
    def _icon_data_func(self, column, cell, model, iter, data):
        """Set the icon based on the icon name in the model."""
        icon_name = model.get_value(iter, 2)
        cell.set_property("icon-name", icon_name)
        
    def create_context_menu(self):
        """Create the context menu for file operations."""
        self.context_menu = Gtk.Menu()
        
        # Open file
        open_item = Gtk.MenuItem.new_with_label("Open")
        open_item.connect("activate", self.on_open_file)
        self.context_menu.append(open_item)
        
        # Copy path
        copy_path_item = Gtk.MenuItem.new_with_label("Copy Path")
        copy_path_item.connect("activate", self.on_copy_path)
        self.context_menu.append(copy_path_item)
        
        # Rename file
        rename_item = Gtk.MenuItem.new_with_label("Rename")
        rename_item.connect("activate", self.on_rename_file)
        self.context_menu.append(rename_item)
        
    def on_right_click(self, treeview, event):
        """Handle right-click event on the tree view."""
        if event.button == 3:  # Right button
            path_info = treeview.get_path_at_pos(int(event.x), int(event.y))
            if path_info is not None:
                path, column, cell_x, cell_y = path_info
                
                # Select the right-clicked row
                treeview.set_cursor(path, column, False)
                
                # Get selected item
                model = treeview.get_model()
                iter = model.get_iter(path)
                file_path = model.get_value(iter, 1)
                is_dir = model.get_value(iter, 4)
                
                # Create and populate popup menu
                menu = Gtk.PopoverMenu.new()
                
                # Create menu model
                menu_model = Gio.Menu.new()
                
                # Add item for opening files
                if not is_dir:
                    open_action = Gio.SimpleAction.new("open", None)
                    open_action.connect("activate", 
                                       lambda a, p: self.on_open_file_action(file_path))
                    self.window.add_action(open_action)
                    menu_model.append("Open", "win.open")
                
                # Add item for opening directory in file manager
                if is_dir:
                    browse_action = Gio.SimpleAction.new("browse", None)
                    browse_action.connect("activate",
                                        lambda a, p: self.on_browse_directory(file_path))
                    self.window.add_action(browse_action)
                    menu_model.append("Open in File Manager", "win.browse")
                
                # Add copy path item
                copy_action = Gio.SimpleAction.new("copy-path", None)
                copy_action.connect("activate",
                                  lambda a, p: self.on_copy_path_action(file_path))
                self.window.add_action(copy_action)
                menu_model.append("Copy Path", "win.copy-path")
                
                # Add rename item
                rename_action = Gio.SimpleAction.new("rename", None)
                rename_action.connect("activate",
                                    lambda a, p: self.on_rename_file_action(file_path))
                self.window.add_action(rename_action)
                menu_model.append("Rename", "win.rename")
                
                # Set the menu model
                menu.set_menu_model(menu_model)
                
                # Show the menu
                menu.set_parent(treeview)
                menu.set_pointing_to(Gdk.Rectangle(x=int(event.x), y=int(event.y), width=1, height=1))
                menu.popup()
                
                return True
                
        return False
    
    def on_rename_file(self, menuitem):
        """Handle rename option from context menu."""
        selection = self.file_tree.get_selection()
        model, iter = selection.get_selected()
        
        if iter:
            file_path = model.get_value(iter, 1)
            file_name = model.get_value(iter, 0)
            
            # Create rename dialog
            dialog = Gtk.Dialog(
                title="Rename File",
                parent=self.window,
                flags=0
            )
            dialog.add_button("Cancel", Gtk.ResponseType.CANCEL)
            dialog.add_button("Rename", Gtk.ResponseType.OK)
            dialog.set_default_response(Gtk.ResponseType.OK)
            
            # Add entry for new name
            box = dialog.get_content_area()
            box.set_spacing(6)
            box.set_margin_start(12)
            box.set_margin_end(12)
            box.set_margin_top(12)
            box.set_margin_bottom(12)
            
            label = Gtk.Label(label="New name:")
            box.append(label)
            
            entry = Gtk.Entry()
            entry.set_text(file_name)
            entry.set_activates_default(True)
            box.append(entry)
            
            # Show the dialog
            dialog.connect("response", 
                         lambda d, r: self.on_rename_response(d, r, file_path, entry.get_text()))
            dialog.show()
    
    def on_rename_file_action(self, file_path):
        """Handle rename action from context menu."""
        model = self.file_tree.get_model()
        for row in self._iter_model(model):
            iter, values = row
            if values[1] == file_path:
                file_name = values[0]
                
                # Create rename dialog
                dialog = Gtk.Dialog(
                    title="Rename File",
                    parent=self.window,
                    flags=0
                )
                dialog.add_button("Cancel", Gtk.ResponseType.CANCEL)
                dialog.add_button("Rename", Gtk.ResponseType.OK)
                dialog.set_default_response(Gtk.ResponseType.OK)
                
                # Add entry for new name
                box = dialog.get_content_area()
                box.set_spacing(6)
                box.set_margin_start(12)
                box.set_margin_end(12)
                box.set_margin_top(12)
                box.set_margin_bottom(12)
                
                label = Gtk.Label(label="New name:")
                box.append(label)
                
                entry = Gtk.Entry()
                entry.set_text(file_name)
                entry.set_activates_default(True)
                box.append(entry)
                
                # Show the dialog
                dialog.connect("response", 
                             lambda d, r: self.on_rename_response(d, r, file_path, entry.get_text()))
                dialog.show()
                break
    
    def on_rename_response(self, dialog, response, old_path, new_name):
        """Handle response from rename dialog."""
        if response == Gtk.ResponseType.OK:
            try:
                # Get directory and new file path
                directory = os.path.dirname(old_path)
                new_path = os.path.join(directory, new_name)
                
                # Check if the file already exists
                if os.path.exists(new_path):
                    error_dialog = Gtk.MessageDialog(
                        transient_for=dialog,
                        flags=0,
                        message_type=Gtk.MessageType.ERROR,
                        buttons=Gtk.ButtonsType.OK,
                        text="File already exists"
                    )
                    error_dialog.format_secondary_text(
                        f"A file named '{new_name}' already exists in this location."
                    )
                    error_dialog.run()
                    error_dialog.destroy()
                    return
                
                # Rename the file
                os.rename(old_path, new_path)
                
                # Refresh the file tree
                self.refresh_files()
                
                # Update status
                self.window.update_status(f"Renamed to {new_name}")
                
            except OSError as e:
                error_dialog = Gtk.MessageDialog(
                    transient_for=dialog,
                    flags=0,
                    message_type=Gtk.MessageType.ERROR,
                    buttons=Gtk.ButtonsType.OK,
                    text="Error Renaming File"
                )
                error_dialog.format_secondary_text(str(e))
                error_dialog.run()
                error_dialog.destroy()
        
        dialog.destroy()
    
    def on_copy_path(self, menuitem):
        """Copy the selected file path to clipboard."""
        selection = self.file_tree.get_selection()
        model, iter = selection.get_selected()
        
        if iter:
            file_path = model.get_value(iter, 1)
            self.on_copy_path_action(file_path)
    
    def on_copy_path_action(self, file_path):
        """Copy a file path to clipboard."""
        clipboard = Gdk.Display.get_default().get_clipboard()
        clipboard.set_text(file_path)
        self.window.update_status("Path copied to clipboard")
    
    def on_open_file(self, menuitem):
        """Open the selected file."""
        selection = self.file_tree.get_selection()
        model, iter = selection.get_selected()
        
        if iter:
            file_path = model.get_value(iter, 1)
            self.on_open_file_action(file_path)
    
    def on_open_file_action(self, file_path):
        """Open a file."""
        self.emit("file-activated", file_path)
    
    def on_browse_directory(self, directory_path):
        """Open the directory in the file manager."""
        try:
            Gio.AppInfo.launch_default_for_uri(
                f"file://{directory_path}", None)
        except GLib.Error as e:
            self.window.update_status(f"Error opening directory: {e.message}")
    
    def setup_drag_and_drop(self):
        """Set up drag and drop for the file tree."""
        # TODO: Implement drag and drop
        pass
    
    def set_project_directory(self, directory):
        """Set the project directory and refresh files.
        
        Args:
            directory: Directory to set as project root
            
        Returns:
            bool: True if successful, False otherwise
        """
        if not directory or not os.path.isdir(directory):
            self.window.update_status("Invalid project directory")
            return False
        
        self.project_directory = directory
        
        # Update directory label with the last component of the path
        dir_name = os.path.basename(directory)
        if not dir_name:  # Handle case where directory is the root
            dir_name = directory
        self.directory_label.set_text(dir_name)
        self.directory_label.set_tooltip_text(directory)
        
        # Refresh files
        self.refresh_files()
        
        self.window.update_status(f"Project directory: {dir_name}")
        return True
    
    def refresh_files(self):
        """Refresh the file tree with current project files."""
        # Clear the current tree
        self.file_store.clear()
        
        if not self.project_directory or not os.path.isdir(self.project_directory):
            return
        
        # Add files to the tree
        self._add_directory_to_tree(None, self.project_directory)
        
        # Expand first level
        self.file_tree.expand_row(Gtk.TreePath.new_first(), False)
        
        # Apply current filter
        self.on_filter_changed(self.filter_entry)
    
    def _add_directory_to_tree(self, parent_iter, directory):
        """Add a directory and its contents to the tree.
        
        Args:
            parent_iter: Parent tree iterator or None for root
            directory: Directory to add
        """
        try:
            # Get directory entries
            entries = os.listdir(directory)
            
            # Sort directories first, then files
            dirs = sorted([e for e in entries if os.path.isdir(os.path.join(directory, e))])
            files = sorted([e for e in entries if os.path.isfile(os.path.join(directory, e))])
            
            # Filter hidden files and directories
            dirs = [d for d in dirs if not d.startswith('.')]
            files = [f for f in files if not f.startswith('.')]
            
            # Add directories first
            for dir_name in dirs:
                dir_path = os.path.join(directory, dir_name)
                
                # Add directory to tree
                dir_iter = self.file_store.append(parent_iter, 
                                                [dir_name, dir_path, "folder-symbolic", False, True])
                
                # Recursively add subdirectories
                self._add_directory_to_tree(dir_iter, dir_path)
            
            # Then add files
            for file_name in files:
                file_path = os.path.join(directory, file_name)
                
                # Get file icon based on extension
                icon_name = self._get_file_icon(file_name)
                
                # Add to tree
                self.file_store.append(parent_iter, 
                                     [file_name, file_path, icon_name, False, False])
                
        except OSError as e:
            print(f"Error reading directory {directory}: {e}")
    
    def _get_file_icon(self, filename):
        """Get an icon name for a file based on its extension."""
        ext = os.path.splitext(filename.lower())[1]
        
        # Map extensions to icon names
        icon_map = {
            '.tex': 'text-x-tex-symbolic',
            '.bib': 'text-x-bibtex-symbolic',
            '.pdf': 'application-pdf-symbolic',
            '.png': 'image-x-generic-symbolic',
            '.jpg': 'image-x-generic-symbolic',
            '.jpeg': 'image-x-generic-symbolic',
            '.svg': 'image-x-generic-symbolic',
            '.txt': 'text-x-generic-symbolic',
        }
        
        return icon_map.get(ext, 'text-x-generic-symbolic')
    
    def _iter_model(self, model, parent=None):
        """Iterate through all items in a TreeModel.
        
        Yields:
            tuple: (iter, values) for each row
        """
        if parent is None:
            iter = model.get_iter_first()
        else:
            iter = model.iter_children(parent)
        
        while iter:
            values = []
            for i in range(model.get_n_columns()):
                values.append(model.get_value(iter, i))
            
            yield (iter, values)
            
            # Check for children
            if model.iter_has_child(iter):
                for child_row in self._iter_model(model, iter):
                    yield child_row
            
            iter = model.iter_next(iter)
    
    def on_refresh_clicked(self, button):
        """Handle refresh button click."""
        self.refresh_files()
        self.window.update_status("Project files refreshed")
    
    def on_row_activated(self, treeview, path, column):
        """Handle double-click or Enter key on a tree row."""
        model = treeview.get_model()
        iter = model.get_iter(path)
        
        if iter:
            file_path = model.get_value(iter, 1)
            is_dir = model.get_value(iter, 4)
            
            if is_dir:
                # Toggle expansion state for directories
                if treeview.row_expanded(path):
                    treeview.collapse_row(path)
                else:
                    treeview.expand_row(path, False)
            else:
                # Activate file
                self.emit("file-activated", file_path)
    
    def on_filter_changed(self, entry):
        """Filter the file tree based on the filter entry text."""
        filter_text = entry.get_text().lower()
        
        def filter_tree(model, iter, filter_text):
            """Recursively filter the tree.
            
            Returns:
                bool: True if this node or any children match the filter
            """
            # Get values for this row
            file_name = model.get_value(iter, 0).lower()
            is_dir = model.get_value(iter, 4)
            
            # Check if this row matches
            matches = filter_text == "" or filter_text in file_name
            
            # Mark if this row is filtered out
            model.set_value(iter, 3, not matches and not is_dir)
            
            # Check children for matches
            has_matching_child = False
            child_iter = model.iter_children(iter)
            while child_iter:
                if filter_tree(model, child_iter, filter_text):
                    has_matching_child = True
                child_iter = model.iter_next(child_iter)
            
            # If this is a directory, show it if it has matching children
            if is_dir:
                model.set_value(iter, 3, not has_matching_child and not matches)
                return matches or has_matching_child
            
            return matches
        
        # Start filtering from the root
        iter = self.file_store.get_iter_first()
        while iter:
            filter_tree(self.file_store, iter, filter_text)
            iter = self.file_store.iter_next(iter)
        
        # Update tree visibility based on filter
        def update_row_visibility(model, path, iter, user_data):
            is_filtered = model.get_value(iter, 3)
            row = self.file_tree.get_row_at_path(path)
            if row:
                row.set_visible(not is_filtered)
            return False
        
        self.file_store.foreach(update_row_visibility, None)
        
        # Expand all directories if filtering
        if filter_text:
            self.file_tree.expand_all()
gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, Adw, GObject, Gio, GLib, Gdk


class ProjectSidebar(Gtk.Box):
    """Project sidebar for navigating LaTeX project files"""
    
    __gsignals__ = {
        'file-activated': (GObject.SignalFlags.RUN_FIRST, None, (str,)),
    }
    
    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        self.set_size_request(250, -1)
        
        # Current project directory
        self.project_directory = None
        
        # Create UI components
        self.create_toolbar()
        self.create_file_tree()
        
        # Add CSS class for styling
        self.add_css_class("sidebar")
    
    def create_toolbar(self):
        """Create the sidebar toolbar"""
        toolbar = Gtk.Box(css_classes=["toolbar"])
        toolbar.set_spacing(8)
        
        # Project label with current directory
        self.directory_label = Gtk.Label(label="No Project")
        self.directory_label.set_ellipsize(3)  # Ellipsize at end
        self.directory_label.set_xalign(0)
        self.directory_label.set_hexpand(True)
        toolbar.append(self.directory_label)
        
        # Refresh button
        refresh_button = Gtk.Button(icon_name="view-refresh-symbolic")
        refresh_button.set_tooltip_text("Refresh Project Files")
        refresh_button.connect("clicked", self.on_refresh_clicked)
        toolbar.append(refresh_button)
        
        self.append(toolbar)
    
    def create_file_tree(self):
        """Create the file tree view"""
        # Create a scrolled window
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scrolled.set_vexpand(True)
        
        # Create file store model
        self.file_store = Gtk.TreeStore(
            str,    # Icon name
            str,    # Display name
            str,    # Full path
            bool,   # Is directory
            bool    # Is LaTeX file
        )
        
        # Create and configure the tree view
        self.file_tree = Gtk.TreeView(model=self.file_store)
        self.file_tree.set_headers_visible(False)
        
        # Connect signals
        self.file_tree.connect("row-activated", self.on_row_activated)
        
        # Create drag and drop support
        self.setup_drag_and_drop()
        
        # Create columns
        renderer = Gtk.CellRendererPixbuf()
        column = Gtk.TreeViewColumn("Icon", renderer, icon_name=0)
        self.file_tree.append_column(column)
        
        renderer = Gtk.CellRendererText()
        column = Gtk.TreeViewColumn("Name", renderer, text=1)
        self.file_tree.append_column(column)
        
        # Add tree view to scrolled window
        scrolled.set_child(self.file_tree)
        
        # Add file filter entry
        filter_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        filter_box.set_spacing(4)
        filter_box.set_margin_top(4)
        filter_box.set_margin_bottom(4)
        filter_box.set_margin_start(4)
        filter_box.set_margin_end(4)
        
        filter_icon = Gtk.Image.new_from_icon_name("system-search-symbolic")
        filter_box.append(filter_icon)
        
        self.filter_entry = Gtk.Entry()
        self.filter_entry.set_placeholder_text("Filter files...")
        self.filter_entry.set_hexpand(True)
        self.filter_entry.connect("changed", self.on_filter_changed)
        filter_box.append(self.filter_entry)
        
        # Build the UI
        self.append(filter_box)
        self.append(scrolled)
        
        # Create context menu
        self.create_context_menu()
    
    def create_context_menu(self):
        """Create context menu for the file tree"""
        # Create a gesture controller for right-click
        gesture = Gtk.GestureClick.new()
        gesture.set_button(3)  # Right mouse button
        gesture.connect("pressed", self.on_right_click)
        self.file_tree.add_controller(gesture)
    
    def on_right_click(self, gesture, n_press, x, y):
        """Handle right-click on the file tree"""
        # Find the tree path at the coordinates
        result = self.file_tree.get_path_at_pos(x, y)
        if result:
            path, column, cell_x, cell_y = result
            
            # Select the right-clicked row
            self.file_tree.set_cursor(path, column, False)
            
            # Get the file or directory info
            iter = self.file_store.get_iter(path)
            file_path = self.file_store.get_value(iter, 2)
            is_dir = self.file_store.get_value(iter, 3)
            
            # Create the popup menu
            popover = Gtk.Popover()
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            box.set_margin_top(4)
            box.set_margin_bottom(4)
            box.set_margin_start(4)
            box.set_margin_end(4)
            box.set_spacing(4)
            
            # Open button
            open_button = Gtk.Button(label="Open")
            open_button.connect("clicked", lambda b: self.on_open_file(file_path))
            box.append(open_button)
            
            if not is_dir:
                # Copy path button
                copy_button = Gtk.Button(label="Copy Path")
                copy_button.connect("clicked", lambda b: self.on_copy_path(file_path))
                box.append(copy_button)
            
            if os.path.basename(file_path) not in [".", ".."]:
                # Rename button
                rename_button = Gtk.Button(label="Rename")
                rename_button.connect("clicked", lambda b: self.on_rename_file(file_path, is_dir))
                box.append(rename_button)
            
            # Set popover content and show it
            popover.set_child(box)
            popover.set_parent(self.file_tree)
            popover.set_pointing_to(Gdk.Rectangle(x=x, y=y, width=1, height=1))
            popover.popup()
    
    def on_rename_file(self, file_path, is_dir):
        """Handle rename file action"""
        # Show a dialog to get the new name
        dialog = Adw.MessageDialog(
            transient_for=self.get_root(),
            heading="Rename" + (" Folder" if is_dir else " File"),
            body=f"Enter new name for {os.path.basename(file_path)}:",
        )
        
        # Add an entry for the new name
        entry = Gtk.Entry()
        entry.set_text(os.path.basename(file_path))
        entry.set_activates_default(True)
        entry.set_margin_top(12)
        entry.set_margin_bottom(12)
        dialog.set_extra_child(entry)
        
        # Add buttons
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("rename", "Rename")
        dialog.set_response_appearance("rename", Adw.ResponseAppearance.SUGGESTED)
        dialog.set_default_response("rename")
        
        dialog.connect("response", self.on_rename_response, file_path, entry)
        dialog.present()
    
    def on_rename_response(self, dialog, response, file_path, entry):
        """Handle response from rename dialog"""
        if response == "rename":
            new_name = entry.get_text()
            if new_name and new_name != os.path.basename(file_path):
                new_path = os.path.join(os.path.dirname(file_path), new_name)
                try:
                    os.rename(file_path, new_path)
                    self.refresh_files()
                    
                    # Show success message
                    toast = Adw.Toast(title=f"Renamed to {new_name}")
                    parent = self.get_root()
                    if isinstance(parent, Adw.ToastOverlay):
                        parent.add_toast(toast)
                except Exception as e:
                    # Show error message
                    error_dialog = Adw.MessageDialog(
                        transient_for=self.get_root(),
                        heading="Rename Failed",
                        body=str(e),
                    )
                    error_dialog.add_response("ok", "OK")
                    error_dialog.present()
    
    def on_copy_path(self, file_path):
        """Copy file path to clipboard"""
        # Get the clipboard
        clipboard = Gdk.Display.get_default().get_clipboard()
        
        # Set the path as text on the clipboard
        clipboard.set(file_path)
        
        # Show a toast notification
        toast = Adw.Toast(title="Path copied to clipboard")
        parent = self.get_root()
        if isinstance(parent, Adw.ToastOverlay):
            parent.add_toast(toast)
    
    def on_open_file(self, file_path):
        """Open a file from the context menu"""
        if os.path.isdir(file_path):
            self.set_project_directory(file_path)
        else:
            self.emit("file-activated", file_path)
    
    def setup_drag_and_drop(self):
        """Set up drag and drop support for the file tree"""
        # Make the tree view a drag source
        self.file_tree.drag_source_set(
            Gdk.ModifierType.BUTTON1_MASK,
            []
        )
    
    def set_project_directory(self, directory):
        """Set the current project directory and refresh files"""
        if directory and os.path.isdir(directory):
            self.project_directory = directory
            self.directory_label.set_text(os.path.basename(directory))
            self.directory_label.set_tooltip_text(directory)
            self.refresh_files()
        else:
            self.project_directory = None
            self.directory_label.set_text("No Project")
            self.directory_label.set_tooltip_text("")
            self.file_store.clear()
    
    def refresh_files(self):
        """Refresh the file list"""
        self.file_store.clear()
        
        if not self.project_directory:
            return
        
        # Add parent directory
        parent_iter = self.file_store.append(None, [
            "folder-symbolic",
            "..",
            os.path.dirname(self.project_directory),
            True,
            False
        ])
        
        # Walk through the directory and add files/folders
        items = []
        try:
            for item in os.listdir(self.project_directory):
                # Skip hidden files
                if item.startswith('.'):
                    continue
                
                full_path = os.path.join(self.project_directory, item)
                is_dir = os.path.isdir(full_path)
                
                # Determine if it's a LaTeX file
                is_latex = False
                if not is_dir:
                    ext = os.path.splitext(item)[1].lower()
                    is_latex = ext in ['.tex', '.bib', '.cls', '.sty']
                
                # Create item tuple
                items.append((item, full_path, is_dir, is_latex))
            
            # Sort: directories first, then files, all alphabetically
            items.sort(key=lambda x: (not x[2], x[0].lower()))
            
            # Add to the store
            for item, full_path, is_dir, is_latex in items:
                icon_name = "folder-symbolic" if is_dir else "text-x-generic-symbolic"
                if is_latex:
                    icon_name = "text-x-script-symbolic"
                
                self.file_store.append(None, [
                    icon_name,
                    item,
                    full_path,
                    is_dir,
                    is_latex
                ])
                
        except Exception as e:
            print(f"Error loading project files: {e}")
    
    def on_refresh_clicked(self, button):
        """Handle refresh button click"""
        self.refresh_files()
    
    def on_row_activated(self, tree_view, path, column):
        """Handle double-click on a tree row"""
        iter = self.file_store.get_iter(path)
        file_path = self.file_store.get_value(iter, 2)
        is_dir = self.file_store.get_value(iter, 3)
        
        if is_dir:
            # Open the directory in the sidebar
            self.set_project_directory(file_path)
        else:
            # Emit signal to open the file
            self.emit("file-activated", file_path)
    
    def on_filter_changed(self, entry):
        """Handle changes to the filter entry"""
        filter_text = entry.get_text().lower()
        
        # If filter is empty, just refresh the normal view
        if not filter_text:
            self.refresh_files()
            return
        
        # Clear the store for filtering
        self.file_store.clear()
        
        if not self.project_directory:
            return
        
        # Walk through directories and find matching files
        for root, dirs, files in os.walk(self.project_directory):
            # Skip hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]
            
            # Add matching files
            for file in files:
                if file.startswith('.'):
                    continue
                
                if filter_text in file.lower():
                    full_path = os.path.join(root, file)
                    rel_path = os.path.relpath(full_path, self.project_directory)
                    
                    # Determine if it's a LaTeX file
                    ext = os.path.splitext(file)[1].lower()
                    is_latex = ext in ['.tex', '.bib', '.cls', '.sty']
                    
                    # Set icon based on file type
                    icon_name = "text-x-generic-symbolic"
                    if is_latex:
                        icon_name = "text-x-script-symbolic"
                    
                    self.file_store.append(None, [
                        icon_name,
                        rel_path,  # Show relative path in filtered view
                        full_path,
                        False,
                        is_latex
                    ])
