    def open_document(self):
        """Open a document"""
        # Check for unsaved changes first
        if self.editor_view.is_modified():
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Save Changes?",
                body="The current document has unsaved changes. Would you like to save them?",
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard")
            dialog.add_response("save", "Save")
            dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
            dialog.set_default_response("save")
            
            dialog.connect("response", self.on_open_document_save_response)
            dialog.present()
            return
        
        # Show file chooser dialog
        self.show_file_chooser_dialog(Gtk.FileChooserAction.OPEN, self.on_file_open_response)
    
    def on_open_document_save_response(self, dialog, response):
        """Handle save changes dialog response when opening a document"""
        if response == "save":
            # Save the document and then open
            saved = self.save_document()
            if saved:
                self.open_document()
        elif response == "discard":
            # Open without saving
            self.open_document()
        # else: Cancel - do nothing
    
    def show_file_chooser_dialog(self, action, callback):
        """Show a file chooser dialog"""
        # GTK4 file chooser
        dialog = Gtk.FileDialog()
        
        # Set dialog properties based on action
        if action == Gtk.FileChooserAction.OPEN:
            dialog.set_title("Open LaTeX Document")
            dialog.open(self, None, callback)
        elif action == Gtk.FileChooserAction.SAVE:
            dialog.set_title("Save LaTeX Document")
            if self.current_document and self.current_document.get_filename():
                dialog.set_initial_name(os.path.basename(self.current_document.get_filename()))
            else:
                dialog.set_initial_name("document.tex")
            dialog.save(self, None, callback)
    
    def on_file_open_response(self, dialog, result):
        """Handle file open dialog response"""
        try:
            file = dialog.open_finish(result)
            if file:
                # Get the path from the file
                file_path = file.get_path()
                self.load_document(file_path)
        except GLib.Error as error:
            toast = Adw.Toast(title=f"Error opening file: {error.message}")
            self.toast_overlay.add_toast(toast)
    
    def load_document(self, file_path):
        """Load a document from a file"""
        try:
            # Create new document from file
            self.current_document = Document(file_path)
            
            # Load content into editor
            content = self.current_document.get_content()
            self.editor_view.set_text(content)
            self.editor_view.get_buffer().set_modified(False)
            
            # Update UI
            self.update_title()
            
            # Update project sidebar
            project_dir = os.path.dirname(file_path)
            self.project_sidebar.set_project_directory(project_dir)
            
            # Trigger preview update
            self.update_preview(content)
            
            # Show success message
            filename = os.path.basename(file_path)
            toast = Adw.Toast(title=f"Opened {filename}")
            self.toast_overlay.add_toast(toast)
            
            # Update recent files
            self.add_to_recent_files(file_path)
            
            return True
        except Exception as e:
            # Show error message
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Error Opening File",
                body=f"Could not open file: {str(e)}",
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return False
    
    def save_document(self):
        """Save the document"""
        # If no filename set, save as
        if not self.current_document or not self.current_document.get_filename():
            return self.save_document_as()
        
        # Save content to file
        content = self.editor_view.get_text()
        try:
            self.current_document.set_content(content)
            self.current_document.save()
            self.editor_view.get_buffer().set_modified(False)
            
            # Update UI
            self.update_title()
            
            # Show success message
            filename = os.path.basename(self.current_document.get_filename())
            toast = Adw.Toast(title=f"Saved {filename}")
            self.toast_overlay.add_toast(toast)
            
            return True
        except Exception as e:
            # Show error message
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Error Saving File",
                body=f"Could not save file: {str(e)}",
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return False
    
    def save_document_as(self):
        """Save the document with a new name"""
        self.show_file_chooser_dialog(Gtk.FileChooserAction.SAVE, self.on_file_save_response)
        return False  # Will be saved asynchronously
    
    def on_file_save_response(self, dialog, result):
        """Handle file save dialog response"""
        try:
            file = dialog.save_finish(result)
            if file:
                # Get the path from the file
                file_path = file.get_path()
                
                # Create new document if necessary
                if not self.current_document:
                    self.current_document = Document()
                
                # Set the new filename
                self.current_document.set_filename(file_path)
                
                # Save content
                content = self.editor_view.get_text()
                self.current_document.set_content(content)
                self.current_document.save()
                self.editor_view.get_buffer().set_modified(False)
                
                # Update UI
                self.update_title()
                
                # Update project sidebar
                project_dir = os.path.dirname(file_path)
                self.project_sidebar.set_project_directory(project_dir)
                
                # Show success message
                filename = os.path.basename(file_path)
                toast = Adw.Toast(title=f"Saved as {filename}")
                self.toast_overlay.add_toast(toast)
                
                # Update recent files
                self.add_to_recent_files(file_path)
        except GLib.Error as error:
            toast = Adw.Toast(title=f"Error saving file: {error.message}")
            self.toast_overlay.add_toast(toast)
    
    def update_title(self):
        """Update window title based on current document"""
        if self.current_document:
            # Get the basename of the file
            if self.current_document.get_filename():
                basename = os.path.basename(self.current_document.get_filename())
                self.title_widget.set_title(basename)
                self.title_widget.set_subtitle(self.current_document.get_filename())
            else:
                self.title_widget.set_title("Untitled")
                self.title_widget.set_subtitle("Not saved")
            
            # Add asterisk if modified
            if self.editor_view.is_modified():
                current_title = self.title_widget.get_title()
                if not current_title.endswith("*"):
                    self.title_widget.set_title(current_title + "*")
        else:
            self.title_widget.set_title("SilkTex")
            self.title_widget.set_subtitle("")
    
    def compile_document(self):
        """Compile the current LaTeX document"""
        # Save document if needed
        if self.editor_view.is_modified():
            self.save_document()
        
        # Check if document is saved
        if not self.current_document or not self.current_document.get_filename():
            dialog = Adw.MessageDialog(
                transient_for=self,
                heading="Cannot Compile",
                body="Please save the document before compiling.",
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return
        
        # Get the compilation engine
        selected = self.engine_combo.get_selected()
        engine = self.engine_model.get_string(selected)
        
        # Update status
        self.status_label.set_text("Compiling...")
        
        # Ensure we use standard icon names
        self.refresh_button = self.builder.get_object('refresh_button')
        if self.refresh_button:
            self.refresh_button.set_icon_name('view-refresh-symbolic')
            
        # Compile the document
        success, log = self.current_document.compile(engine)
        
        # Update preview
        self.preview_view.refresh()
        
        # Update status
        if success:
            self.status_label.set_text("Compilation successful")
            toast = Adw.Toast(title="Document compiled successfully")
        else:
            self.status_label.set_text("Compilation failed")
            toast = Adw.Toast(title="Compilation failed. See error log for details.")
            toast.set_button_label("View Log")
            toast.connect("button-clicked", lambda t: self.preview_view.show_error_log())
        
        self.toast_overlay.add_toast(toast)
    
    def update_preview(self, content=None):
        """Update the preview with current content"""
        if not content:
            content = self.editor_view.get_text()
        
        # Try to update the preview
        if self.preview_view:
            if self.current_document and self.current_document.get_filename():
                # For saved documents, we can show the compiled PDF
                self.preview_view.set_document(self.current_document)
            else:
                # For unsaved documents, we need to show a rendered preview
                self.preview_view.set_content(content)
        
        # Always return False so that GLib timeout doesn't repeat
        return False
    
    def add_to_recent_files(self, file_path):
        """Add a file to the recent files list"""
        recent_manager = Gtk.RecentManager.get_default()
        uri = GLib.filename_to_uri(file_path, None)
        recent_data = Gtk.RecentData()
        recent_data.display_name = os.path.basename(file_path)
        recent_data.description = "LaTeX Document"
        recent_data.mime_type = "text/x-tex"
        recent_data.app_name = self.get_application().get_application_id()
        recent_data.app_exec = f"{sys.argv[0]} %u"
        recent_manager.add_full(uri, recent_data)
    
    def show_preferences(self):
        """Show preferences dialog"""
        dialog = PreferencesDialog(self)
        dialog.present()
