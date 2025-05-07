        # Create sidebar stack switcher
        self.sidebar_stack_switcher = Gtk.StackSwitcher()
        self.sidebar_stack_switcher.set_stack(self.sidebar_stack)
        self.sidebar_stack_switcher.set_halign(Gtk.Align.CENTER)
        
        # Create sidebar header
        sidebar_header = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        sidebar_header.append(self.sidebar_stack_switcher)
        sidebar_header.add_css_class("sidebar-header")
        
        # Add sidebar header to sidebar
        self.sidebar.append(sidebar_header)
        self.sidebar.append(self.sidebar_stack)
        
        # Create and add sidebar pages
        self.create_structure_page()
        self.create_templates_page()
        self.create_snippets_page()
    
    def create_structure_page(self):
        """Create the document structure page for the sidebar"""
        structure_page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        structure_page.set_margin_top(12)
        structure_page.set_margin_bottom(12)
        structure_page.set_margin_start(12)
        structure_page.set_margin_end(12)
        
        # Create document structure tree view
        self.structure_store = Gtk.TreeStore(str, str)  # Title, Type
        self.structure_view = Gtk.TreeView(model=self.structure_store)
        self.structure_view.set_headers_visible(False)
        
        # Create columns
        title_renderer = Gtk.CellRendererText()
        title_column = Gtk.TreeViewColumn("Title", title_renderer, text=0)
        self.structure_view.append_column(title_column)
        
        # Create scrolled window for tree view
        structure_scroll = Gtk.ScrolledWindow()
        structure_scroll.set_vexpand(True)
        structure_scroll.set_child(self.structure_view)
        
        # Add to structure page
        structure_page.append(structure_scroll)
        
        # Add to sidebar stack
        self.sidebar_stack.add_titled(structure_page, "structure", "Structure")
    
    def create_templates_page(self):
        """Create the templates page for the sidebar"""
        templates_page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        templates_page.set_margin_top(12)
        templates_page.set_margin_bottom(12)
        templates_page.set_margin_start(12)
        templates_page.set_margin_end(12)
        
        # Create templates list
        self.templates_list = Gtk.ListBox()
        self.templates_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.templates_list.set_activate_on_single_click(False)
        self.templates_list.connect("row-activated", self.on_template_activated)
        
        # Create scrolled window for list
        templates_scroll = Gtk.ScrolledWindow()
        templates_scroll.set_vexpand(True)
        templates_scroll.set_child(self.templates_list)
        
        # Add to templates page
        templates_page.append(templates_scroll)
        
        # Add button to manage templates
        manage_templates_button = Gtk.Button.new_with_label("Manage Templates")
        manage_templates_button.set_margin_top(12)
        manage_templates_button.connect("clicked", self.on_manage_templates)
        templates_page.append(manage_templates_button)
        
        # Add to sidebar stack
        self.sidebar_stack.add_titled(templates_page, "templates", "Templates")
        
        # Populate templates list
        self.populate_templates_list()
    
    def create_snippets_page(self):
        """Create the snippets page for the sidebar"""
        snippets_page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        snippets_page.set_margin_top(12)
        snippets_page.set_margin_bottom(12)
        snippets_page.set_margin_start(12)
        snippets_page.set_margin_end(12)
        
        # Create snippets list
        self.snippets_list = Gtk.ListBox()
        self.snippets_list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.snippets_list.set_activate_on_single_click(False)
        self.snippets_list.connect("row-activated", self.on_snippet_activated)
        
        # Create scrolled window for list
        snippets_scroll = Gtk.ScrolledWindow()
        snippets_scroll.set_vexpand(True)
        snippets_scroll.set_child(self.snippets_list)
        
        # Add to snippets page
        snippets_page.append(snippets_scroll)
        
        # Add button to manage snippets
        manage_snippets_button = Gtk.Button.new_with_label("Manage Snippets")
        manage_snippets_button.set_margin_top(12)
        manage_snippets_button.connect("clicked", self.on_manage_snippets)
        snippets_page.append(manage_snippets_button)
        
        # Add to sidebar stack
        self.sidebar_stack.add_titled(snippets_page, "snippets", "Snippets")
        
        # Populate snippets list
        self.populate_snippets_list()
    
    def populate_templates_list(self):
        """Populate the templates list with available templates"""
        # Clear existing items
        while True:
            row = self.templates_list.get_first_child()
            if row is None:
                break
            self.templates_list.remove(row)
        
        # Add templates from template manager
        for template in self.template_manager.get_templates():
            row = Adw.ActionRow()
            row.set_title(template["name"])
            row.set_subtitle(template["description"])
            
            # Store template data in row
            row.template_data = template
            
            self.templates_list.append(row)
    
    def populate_snippets_list(self):
        """Populate the snippets list with available snippets"""
        # Clear existing items
        while True:
            row = self.snippets_list.get_first_child()
            if row is None:
                break
            self.snippets_list.remove(row)
        
        # Add snippets from snippet manager
        for snippet in self.snippet_manager.get_snippets():
            row = Adw.ActionRow()
            row.set_title(snippet["name"])
            row.set_subtitle(snippet["description"])
            
            # Store snippet data in row
            row.snippet_data = snippet
            
            self.snippets_list.append(row)
    
    def on_template_activated(self, list_box, row):
        """Handle template activation
        
        Args:
            list_box: The templates list box
            row: The activated row
        """
        if hasattr(row, "template_data"):
            # Confirm with user if the current document has content
            if self.editor.get_text().strip() and self.modified:
                dialog = Adw.MessageDialog.new(
                    self,
                    "Replace Current Document?",
                    "Loading a template will replace the current document content. Continue?"
                )
                dialog.add_response("cancel", "Cancel")
                dialog.add_response("replace", "Replace")
                dialog.set_response_appearance("replace", Adw.ResponseAppearance.DESTRUCTIVE)
                dialog.set_default_response("cancel")
                
                dialog.connect("response", self.on_template_confirmation, row.template_data)
                dialog.present()
            else:
                # Load template directly if document is empty or not modified
                self.load_template(row.template_data)
    
    def on_template_confirmation(self, dialog, response, template_data):
        """Handle template confirmation dialog response
        
        Args:
            dialog: The dialog
            response: The response ID
            template_data: The template data to load
        """
        if response == "replace":
            self.load_template(template_data)
    
    def load_template(self, template_data):
        """Load a template into the editor
        
        Args:
            template_data: The template data to load
        """
        # Set editor text to template content
        self.editor.set_text(template_data["content"])
        
        # Update window title with template name
        self.set_title(f"SilkTex - New Document from {template_data['name']}")
        
        # Reset file path
        self.current_file = None
        
        # Reset modified flag
        self.modified = False
        
        # Update window state
        self.update_window_state()
    
    def on_snippet_activated(self, list_box, row):
        """Handle snippet activation
        
        Args:
            list_box: The snippets list box
            row: The activated row
        """
        if hasattr(row, "snippet_data"):
            # Insert snippet at current cursor position
            self.editor.insert_text(row.snippet_data["content"])
    
    def on_manage_templates(self, button):
        """Handle manage templates button click"""
        dialog = TemplateDialog(self, self.template_manager)
        dialog.connect("closed", lambda _: self.populate_templates_list())
        dialog.present()
    
    def on_manage_snippets(self, button):
        """Handle manage snippets button click"""
        dialog = SnippetDialog(self, self.snippet_manager)
        dialog.connect("closed", lambda _: self.populate_snippets_list())
        dialog.present()
    
    def setup_actions(self):
        """Set up window actions"""
        # New document action
        new_action = Gio.SimpleAction.new("new", None)
        new_action.connect("activate", self.on_new)
        self.add_action(new_action)
        
        # Open document action
        open_action = Gio.SimpleAction.new("open", None)
        open_action.connect("activate", self.on_open)
        self.add_action(open_action)
        
        # Save document action
        save_action = Gio.SimpleAction.new("save", None)
        save_action.connect("activate", self.on_save)
        self.add_action(save_action)
        
        # Save as document action
        save_as_action = Gio.SimpleAction.new("save-as", None)
        save_as_action.connect("activate", self.on_save_as)
        self.add_action(save_as_action)
        
        # Document compile action
        compile_action = Gio.SimpleAction.new("compile", None)
        compile_action.connect("activate", self.on_compile)
        self.add_action(compile_action)
        
        # Document statistics action
        stats_action = Gio.SimpleAction.new("statistics", None)
        stats_action.connect("activate", self.on_statistics)
        self.add_action(stats_action)
        
        # Preferences action
        prefs_action = Gio.SimpleAction.new("preferences", None)
        prefs_action.connect("activate", self.on_preferences)
        self.add_action(prefs_action)
    
    def load_css(self):
        """Load custom CSS for the application"""
        css_provider = Gtk.CssProvider()
        css = """
        .sidebar {
            border-right: 1px solid @borders;
        }
        
        .sidebar-header {
            padding: 6px;
            border-bottom: 1px solid @borders;
        }
        
        .toolbar-button {
            padding: 4px;
        }
        
        .toolbar-view {
            border-bottom: 1px solid @borders;
        }
        
        .preview-container {
            background-color: @theme_base_color;
        }
        """
        css_provider.load_from_data(css.encode())
        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
    
    def on_text_changed(self, editor):
        """Handle editor text changes
        
        Args:
            editor: The editor widget
        """
        self.modified = True
        self.update_window_state()
        
        # If auto-refresh is enabled, queue compilation
        if self.auto_refresh.get_active():
            # Don't compile too frequently
            current_time = time.time()
            if current_time - self.last_compile_time > self.compile_interval and not self.compilation_in_progress:
                # Schedule a compilation after a delay
                GLib.timeout_add(int(self.compile_interval * 1000), self.auto_compile)
    
    def auto_compile(self):
        """Automatically compile the document in response to changes"""
        if self.compilation_in_progress:
            return False  # Don't compile if already in progress
        
        # Compile the document
        if self.current_file:
            self.compile_document()
        else:
            # For unsaved documents, compile in memory
            self.compile_document(in_memory=True)
        
        return False  # Don't repeat
    
    def on_cursor_moved(self, editor, line, column):
        """Handle editor cursor movements
        
        Args:
            editor: The editor widget
            line: The current line number
            column: The current column number
        """
        # Update status bar cursor position
        self.cursor_position_label.set_text(f"Line: {line}, Col: {column}")
    
    def on_new(self, action, parameter):
        """Handle new document action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        # Check if current document has unsaved changes
        if self.modified:
            self.confirm_close_document(self.new_document)
        else:
            self.new_document()
    
    def new_document(self):
        """Create a new empty document"""
        self.editor.set_text("")
        self.current_file = None
        self.modified = False
        self.set_title("SilkTex - New Document")
        self.update_window_state()
    
    def on_open(self, action, parameter):
        """Handle open document action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        # Check if current document has unsaved changes
        if self.modified:
            self.confirm_close_document(self.show_open_dialog)
        else:
            self.show_open_dialog()
    
    def show_open_dialog(self):
        """Show the open file dialog"""
        dialog = Gtk.FileDialog.new()
        dialog.set_title("Open LaTeX Document")
        
        # Set up file filters
        filters = Gtk.FileFilter.new()
        filters.set_name("LaTeX Files")
        filters.add_pattern("*.tex")
        dialog.set_default_filter(filters)
        
        # Show dialog
        dialog.open(self, None, self.on_open_response)
    
    def on_open_response(self, dialog, result):
        """Handle open dialog response
        
        Args:
            dialog: The dialog
            result: The dialog result
        """
        try:
            file = dialog.open_finish(result)
            if file:
                self.load_file(file.get_path())
        except Exception as e:
            logging.error(f"Error opening file: {str(e)}")
            self.show_toast(f"Error opening file: {str(e)}")
    
    def load_file(self, file_path):
        """Load a file into the editor
        
        Args:
            file_path: The path to the file to load
        """
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                text = f.read()
            
            # Set text in editor
            self.editor.set_text(text)
            
            # Update state
            self.current_file = file_path
            self.modified = False
            
            # Update window title
            self.set_title(f"SilkTex - {os.path.basename(file_path)}")
            
            # Update status
            self.status_label.set_text(f"Loaded: {file_path}")
            
            # Update window state
            self.update_window_state()
            
            # Automatically compile the document
            self.compile_document()
            
            # Update document structure
            self.update_document_structure()
            
        except Exception as e:
            logging.error(f"Error loading file: {str(e)}")
            self.show_toast(f"Error loading file: {str(e)}")
    
    def on_save(self, action, parameter):
        """Handle save document action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        # If no file is associated, show save as dialog
        if self.current_file is None:
            self.on_save_as(action, parameter)
        else:
            self.save_file(self.current_file)
    
    def on_save_as(self, action, parameter):
        """Handle save as document action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        dialog = Gtk.FileDialog.new()
        dialog.set_title("Save LaTeX Document")
        
        # Set up file filters
        filters = Gtk.FileFilter.new()
        filters.set_name("LaTeX Files")
        filters.add_pattern("*.tex")
        dialog.set_default_filter(filters)
        
        # Set initial name if current file exists
        if self.current_file:
            dialog.set_initial_name(os.path.basename(self.current_file))
        else:
            dialog.set_initial_name("document.tex")
        
        # Show dialog
        dialog.save(self, None, self.on_save_response)
    
    def on_save_response(self, dialog, result):
        """Handle save dialog response
        
        Args:
            dialog: The dialog
            result: The dialog result
        """
        try:
            file = dialog.save_finish(result)
            if file:
                self.save_file(file.get_path())
        except Exception as e:
            logging.error(f"Error saving file: {str(e)}")
            self.show_toast(f"Error saving file: {str(e)}")
    
    def save_file(self, file_path):
        """Save the current document to a file
        
        Args:
            file_path: The path to save to
        """
        try:
            # Get text from editor
            text = self.editor.get_text()
            
            # Save to file
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(text)
            
            # Update state
            self.current_file = file_path
            self.modified = False
            
            # Update window title
            self.set_title(f"SilkTex - {os.path.basename(file_path)}")
            
            # Update status
            self.status_label.set_text(f"Saved: {file_path}")
            
            # Update window state
            self.update_window_state()
            
            # Automatically compile the document
            self.compile_document()
            
        except Exception as e:
            logging.error(f"Error saving file: {str(e)}")
            self.show_toast(f"Error saving file: {str(e)}")
    
    def on_compile(self, action, parameter):
        """Handle document compile action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        self.compile_document()
    
    def compile_document(self, in_memory=False):
        """Compile the current document
        
        Args:
            in_memory: Whether to compile an unsaved document in memory
        """
        if self.compilation_in_progress:
            return
            
        self.compilation_in_progress = True
        self.last_compile_time = time.time()
        
        # Update status
        self.status_label.set_text("Compiling document...")
        
        # Get the document text
        text = self.editor.get_text()
        
        # Compile the document
        if in_memory or not self.current_file:
            # Compile unsaved document in memory
            compile_thread = threading.Thread(
                target=self.compiler.compile_text,
                args=(text, self.on_compilation_complete)
            )
        else:
            # Compile saved document
            compile_thread = threading.Thread(
                target=self.compiler.compile_file,
                args=(self.current_file, self.on_compilation_complete)
            )
        
        compile_thread.daemon = True
        compile_thread.start()
    
    def on_compilation_complete(self, success, pdf_path, log_text, error_message):
        """Handle compilation completion
        
        Args:
            success: Whether compilation was successful
            pdf_path: Path to the output PDF file
            log_text: Compilation log text
            error_message: Error message if compilation failed
        """
        # Update state
        self.compilation_in_progress = False
        
        # Update UI on the main thread
        GLib.idle_add(self.update_after_compilation, success, pdf_path, log_text, error_message)
    
    def update_after_compilation(self, success, pdf_path, log_text, error_message):
        """Update the UI after compilation completes
        
        Args:
            success: Whether compilation was successful
            pdf_path: Path to the output PDF file
            log_text: Compilation log text
            error_message: Error message if compilation failed
        """
        if success:
            # Update status
            self.status_label.set_text("Compilation successful")
            
            # Load the PDF file in the preview
            self.preview.load_pdf(pdf_path)
            
            # Parse LaTeX errors and warnings for the editor
            self.parse_latex_output(log_text)
        else:
            # Update status
            self.status_label.set_text(f"Compilation failed: {error_message}")
            
            # Show error in preview
            if hasattr(self.preview, "show_error"):
                self.preview.show_error(error_message)
            
            # Parse LaTeX errors for the editor
            self.parse_latex_output(log_text)
            
            # Show error toast
            self.show_toast(f"Compilation failed: {error_message}")
    
    def parse_latex_output(self, log_text):
        """Parse LaTeX compilation output for errors and warnings
        
        Args:
            log_text: The LaTeX compiler log text
        """
        # TODO: Implement error and warning parsing
        # This will be used to highlight errors in the editor
        pass
    
    def on_statistics(self, action, parameter):
        """Handle document statistics action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        # Get document text
        text = self.editor.get_text()
        
        # Show statistics dialog
        dialog = StatisticsDialog(self, text)
        dialog.present()
    
    def on_preferences(self, action, parameter):
        """Handle preferences action
        
        Args:
            action: The action
            parameter: Action parameters
        """
        # TODO: Implement preferences dialog
        self.show_toast("Preferences not implemented yet")
    
    def confirm_close_document(self, on_confirm):
        """Show confirmation dialog for closing a document with unsaved changes
        
        Args:
            on_confirm: Callback to run if user confirms
        """
        dialog = Adw.MessageDialog.new(
            self,
            "Save Changes?",
            "The current document has unsaved changes. Save before closing?"
        )
        dialog.add_response("discard", "Discard")
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("save", "Save")
        
        dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
        dialog.set_default_response("save")
        
        dialog.connect("response", self.on_close_confirmation, on_confirm)
        dialog.present()
    
    def on_close_confirmation(self, dialog, response, on_confirm):
        """Handle close confirmation dialog response
        
        Args:
            dialog: The dialog
            response: The response ID
            on_confirm: Callback to run if user confirms
        """
        if response == "save":
            # Save and then continue with the action
            if self.current_file:
                self.save_file(self.current_file)
                on_confirm()
            else:
                # Need to show save dialog first
                save_dialog = Gtk.FileDialog.new()
                save_dialog.set_title("Save LaTeX Document")
                
                # Set up file filters
                filters = Gtk.FileFilter.new()
                filters.set_name("LaTeX Files")
                filters.add_pattern("*.tex")
                save_dialog.set_default_filter(filters)
                
                # Set initial name
                save_dialog.set_initial_name("document.tex")
                
                # Show dialog
                save_dialog.save(self, None, lambda d, r: self.on_save_before_close(d, r, on_confirm))
        
        elif response == "discard":
            # Just continue with the action without saving
            on_confirm()
    
    def on_save_before_close(self, dialog, result, on_confirm):
        """Handle saving before closing
        
        Args:
            dialog: The save dialog
            result: The dialog result
            on_confirm: Callback to run after saving
        """
        try:
            file = dialog.save_finish(result)
            if file:
                self.save_file(file.get_path())
                on_confirm()
        except Exception as e:
            logging.error(f"Error saving file: {str(e)}")
            self.show_toast(f"Error saving file: {str(e)}")
    
    def update_window_state(self):
        """Update window state based on current document"""
        # Update title to show modified state
        if self.current_file:
            title = f"SilkTex - {os.path.basename(self.current_file)}"
            if self.modified:
                title += " *"
            self.set_title(title)
        elif self.modified:
            self.set_title("SilkTex - New Document *")
    
    def update_document_structure(self):
        """Update the document structure tree view with the current document structure"""
        # Clear the structure store
        self.structure_store.clear()
        
        # Get document text
        text = self.editor.get_text()
        
        # Parse the document structure
        from re import finditer
        
        # Find document title
        doc_title = "Document"
        title_match = re.search(r'\\title\{([^}]+)\}', text)
        if title_match:
            doc_title = title_match.group(1)
        
        # Add document root
        doc_iter = self.structure_store.append(None, [doc_title, "document"])
        
        # Find sections
        for section_match in finditer(r'\\section\{([^}]+)\}', text):
            section_title = section_match.group(1)
            section_iter = self.structure_store.append(doc_iter, [section_title, "section"])
            
            # Find subsections within this section's text range
            section_start = section_match.end()
            next_section = re.search(r'\\section\{', text[section_start:])
            section_end = next_section.start() + section_start if next_section else len(text)
            section_text = text[section_start:section_end]
            
            # Add subsections
            for subsection_match in finditer(r'\\subsection\{([^}]+)\}', section_text):
                subsection_title = subsection_match.group(1)
                subsection_iter = self.structure_store.append(section_iter, [subsection_title, "subsection"])
                
                # Find subsubsections
                subsection_start = subsection_match.end()
                next_subsection = re.search(r'\\subsection\{', section_text[subsection_start:])
                subsection_end = next_subsection.start() + subsection_start if next_subsection else len(section_text)
                subsection_text = section_text[subsection_start:subsection_end]
                
                # Add subsubsections
                for subsubsection_match in finditer(r'\\subsubsection\{([^}]+)\}', subsection_text):
                    subsubsection_title = subsubsection_match.group(1)
                    self.structure_store.append(subsection_iter, [subsubsection_title, "subsubsection"])
    
    def show_toast(self, message):
        """Show a toast notification
        
        Args:
            message: The message to show
        """
        toast = Adw.Toast.new(message)
        toast.set_timeout(3)
        self.toast_overlay.add_toast(toast)
