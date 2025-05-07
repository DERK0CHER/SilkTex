# project_sidebar.py - Project sidebar for file navigation
import os
import gi

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
