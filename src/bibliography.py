    def scan_document_for_citations(self):
        """Scan the current document for citations and suggest bibliography entries."""
        if not self.window.editor:
            return
        
        buffer = self.window.editor.buffer
        start = buffer.get_start_iter()
        end = buffer.get_end_iter()
        text = buffer.get_text(start, end, False)
        
        # Find all citations in the document
        cite_pattern = r'\\(?:cite|citep|citet|citeyear|citeauthor|footcite|parencite|textcite)[*]?(?:\[[^\]]*\])?{([^}]*)}'
        citations = []
        
        for match in re.finditer(cite_pattern, text):
            # Extract the citation keys (may be multiple keys separated by commas)
            keys = match.group(1).split(',')
            for key in keys:
                key = key.strip()
                if key and key not in citations:
                    citations.append(key)
        
        # If we don't have a bibliography file yet, prompt to create one
        if not self.current_bib_file:
            if len(citations) > 0:
                self._prompt_create_bibliography(citations)
            else:
                # Show message that no citations were found
                message = Adw.MessageDialog.new(
                    self.window,
                    "No Citations Found",
                    "No citation commands were found in the document."
                )
                message.add_response("ok", "OK")
                message.present()
        else:
            # Check if any citations are missing from current bibliography
            if len(citations) > 0:
                existing_keys = [entry['key'] for entry in self.entries]
                missing_keys = [key for key in citations if key not in existing_keys]
                
                if missing_keys:
                    self._show_missing_citations_dialog(missing_keys)
                else:
                    # Show a confirmation that all citations are present
                    self.window.update_status(f"All {len(citations)} citations found in bibliography")
            else:
                # Show message that no citations were found
                message = Adw.MessageDialog.new(
                    self.window,
                    "No Citations Found",
                    "No citation commands were found in the document."
                )
                message.add_response("ok", "OK")
                message.present()
    
    def _prompt_create_bibliography(self, citations):
        """Prompt the user to create a bibliography file.
        
        Args:
            citations: List of citation keys found in the document
        """
        # Create dialog
        message = Adw.MessageDialog.new(
            self.window,
            "Create Bibliography File?",
            f"Found {len(citations)} citation(s) but no bibliography file is associated with this document. "
            "Would you like to create a new bibliography file?"
        )
        message.add_response("cancel", "Cancel")
        message.add_response("create", "Create Bibliography")
        message.set_default_response("create")
        
        # Connect response handler
        message.connect("response", self._on_create_bibliography_response, citations)
        message.present()
    
    def _on_create_bibliography_response(self, dialog, response, citations):
        """Handle response from create bibliography dialog.
        
        Args:
            dialog: The dialog
            response: The response
            citations: List of citation keys
        """
        if response == "create":
            # Create a file dialog to save the new bibliography file
            file_dialog = Gtk.FileDialog.new()
            file_dialog.set_title("Create Bibliography File")
            
            # Set initial filename
            if self.window.editor.filename:
                basename = os.path.splitext(os.path.basename(self.window.editor.filename))[0]
                file_dialog.set_initial_name(f"{basename}.bib")
            else:
                file_dialog.set_initial_name("bibliography.bib")
            
            # Create file filter for .bib files
            filter_bib = Gtk.FileFilter()
            filter_bib.set_name("BibTeX Files")
            filter_bib.add_pattern("*.bib")
            
            filters = Gio.ListStore.new(Gtk.FileFilter)
            filters.append(filter_bib)
            file_dialog.set_filters(filters)
            
            # Show the dialog and handle response
            file_dialog.save(self.window, None, self._on_create_bibliography_file_response, citations)
        
        dialog.destroy()
    
    def _on_create_bibliography_file_response(self, dialog, result, citations):
        """Handle response from create bibliography file dialog.
        
        Args:
            dialog: The dialog
            result: The result
            citations: List of citation keys
        """
        try:
            file = dialog.save_finish(result)
            if file:
                filepath = file.get_path()
                
                # Create initial empty bibliography entries for the citations
                entries = []
                for key in citations:
                    entry = {
                        'type': 'article',  # Default type
                        'key': key,
                        'fields': {
                            'author': '',
                            'title': '',
                            'journal': '',
                            'year': str(GLib.DateTime.new_now_local().get_year()),
                            'note': 'Please complete this bibliography entry'
                        }
                    }
                    entries.append(entry)
                
                # Generate content and save to file
                content = self.generate_bib_content(entries)
                
                with open(filepath, 'w', encoding='utf-8') as f:
                    f.write(content)
                
                # Load the new file
                self._load_entries_from_file(filepath)
                
                # Show the bibliography manager
                self.show_bibliography_manager()
                    
                    def export_bibliography(self):
                        """Export the bibliography to a BibTeX file."""
                        if not self.entries:
                            # Show error message
                            message = Adw.MessageDialog.new(
                self.window,
                "No Bibliography Entries",
                "There are no bibliography entries to export."
                            )
                            message.add_response("ok", "OK")
                            message.present()
                            return
                        
                        # Create file dialog
                        file_dialog = Gtk.FileDialog.new()
                        file_dialog.set_title("Export Bibliography")
                        
                        # Set initial filename
                        if self.current_bib_file:
                            basename = os.path.basename(self.current_bib_file)
                            file_dialog.set_initial_name(basename)
                        else:
                            file_dialog.set_initial_name("bibliography.bib")
                        
                        # Create file filter for .bib files
                        filter_bib = Gtk.FileFilter()
                        filter_bib.set_name("BibTeX Files")
                        filter_bib.add_pattern("*.bib")
                        
                        filters = Gio.ListStore.new(Gtk.FileFilter)
                        filters.append(filter_bib)
                        file_dialog.set_filters(filters)
                        
                        # Show the dialog and handle response
                        file_dialog.save(self.window, None, self._on_export_bibliography_response)
                    
                    def _on_export_bibliography_response(self, dialog, result):
                        """Handle response from export bibliography dialog.
                        
                        Args:
                            dialog: The dialog
                            result: The result
                        """
                        try:
                            file = dialog.save_finish(result)
                            if file:
                filepath = file.get_path()
                
                # Generate BibTeX content
                content = self.generate_bib_content(self.entries)
                
                # Write to file
                with open(filepath, 'w', encoding='utf-8') as f:
                    f.write(content)
                
                # Update status
                self.window.update_status(f"Exported {len(self.entries)} bibliography entries to {os.path.basename(filepath)}")
                        
                        except Exception as e:
                            print(f"Error exporting bibliography: {e}")
                            self.window.update_status("Error exporting bibliography")
                                
                                def validate_bibtex_format(self):
                                    """Validate the BibTeX format of the current bibliography entries."""
                                    if not self.entries:
                            # Show error message
                            message = Adw.MessageDialog.new(
                                self.window,
                                "No Bibliography Entries",
                                "There are no bibliography entries to validate."
                            )
                            message.add_response("ok", "OK")
                            message.present()
                            return
                                    
                                    errors = []
                                    warnings = []
                                    
                                    # Check each entry
                                    for entry in self.entries:
                            entry_key = entry['key']
                            entry_type = entry['type']
                            
                            # Check if required fields are present and not empty
                            if entry_type in self.entry_types:
                                for field in self.entry_types[entry_type]:
                                    if field not in entry['fields'] or not entry['fields'][field].strip():
                                        errors.append(f"Entry '{entry_key}' is missing required field '{field}'")
                            
                            # Check for invalid characters in keys
                            if not re.match(r'^[a-zA-Z0-9_\-:\.]+$', entry_key):
                                warnings.append(f"Entry key '{entry_key}' contains invalid characters. " 
                                              f"Keys should only contain letters, numbers, and the characters _-:.")
                            
                            # Check for potentially problematic characters in field values
                            for field, value in entry['fields'].items():
                                if '{' in value and '}' not in value:
                                    warnings.append(f"Entry '{entry_key}', field '{field}' has unbalanced braces")
                                
                                if '\\' in value and not ('{\\' in value or '\\\\' in value):
                                    warnings.append(f"Entry '{entry_key}', field '{field}' has potentially problematic backslash")
                                    
                                    # Create result message
                                    if not errors and not warnings:
                            result_message = "No errors or warnings found. Your bibliography is well-formatted."
                            title = "Validation Successful"
                                    else:
                            result_message = ""
                            if errors:
                                result_message += "Errors:\n"
                                for error in errors:
                                    result_message += f"• {error}\n"
                            
                            if warnings:
                                if errors:
                                    result_message += "\n"
                                result_message += "Warnings:\n"
                                for warning in warnings:
                                    result_message += f"• {warning}\n"
                            
                            title = f"Validation Found {len(errors)} Errors, {len(warnings)} Warnings"
                                    
                                    # Show validation results
                                    message = Adw.MessageDialog.new(
                            self.window,
                            title,
                            result_message
                                    )
                                    message.add_response("ok", "OK")
                                    
                                    # Add fix button if there are errors or warnings
                                    if errors or warnings:
                            message.add_response("fix", "Edit Entries")
                            message.connect("response", self._on_validation_response)
                                    
                                    message.present()
                                
                                def _on_validation_response(self, dialog, response):
                                    """Handle response from validation dialog.
                                    
                                    Args:
                            dialog: The dialog
                            response: The response
                                    """
                                    if response == "fix":
                            # Show bibliography manager to fix issues
                            self.show_bibliography_manager()
                                    
                                    dialog.destroy()
                
                # Update status
                self.window.update_status(f"Created bibliography file with {len(citations)} entries")
                
        except Exception as e:
            print(f"Error creating bibliography file: {e}")
            self.window.update_status("Error creating bibliography file")
    
    def _show_missing_citations_dialog(self, missing_keys):
        """Show a dialog for missing citations.
        
        Args:
            missing_keys: List of missing citation keys
        """
        # Create dialog
        message = Adw.MessageDialog.new(
            self.window,
            "Missing Citations",
            f"Found {len(missing_keys)} citation(s) that are not in your bibliography:\n\n" +
            ", ".join(missing_keys) + "\n\nWould you like to add these entries to your bibliography?"
        )
        message.add_response("cancel", "Cancel")
        message.add_response("add", "Add Entries")
        message.set_default_response("add")
        
        # Connect response handler
        message.connect("response", self._on_add_missing_citations_response, missing_keys)
        message.present()
    
    def _on_add_missing_citations_response(self, dialog, response, missing_keys):
        """Handle response from add missing citations dialog.
        
        Args:
            dialog: The dialog
            response: The response
            missing_keys: List of missing citation keys
        """
        if response == "add":
            # Add entries for missing keys
            for key in missing_keys:
                entry = {
                    'type': 'article',  # Default type
                    'key': key,
                    'fields': {
                        'author': '',
                        'title': '',
                        'journal': '',
                        'year': str(GLib.DateTime.new_now_local().get_year()),
                        'note': 'Please complete this bibliography entry'
                    }
                }
                self.entries.append(entry)
            
            # Save to file
            self._save_entries_to_file()
            
            # Reload entries in the UI
            self._load_entries_from_file(self.current_bib_file)
            
            # Show the bibliography manager
            self.show_bibliography_manager()
            
            # Update status
            self.window.update_status(f"Added {len(missing_keys)} entries to bibliography")
        
        dialog.destroy()
    
    def import_from_bibtex(self):
        """Import entries from a BibTeX file."""
        # Create file dialog
        file_dialog = Gtk.FileDialog.new()
        file_dialog.set_title("Import BibTeX File")
        
        # Create file filter for .bib files
        filter_bib = Gtk.FileFilter()
        filter_bib.set_name("BibTeX Files")
        filter_bib.add_pattern("*.bib")
        
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(filter_bib)
        file_dialog.set_filters(filters)
        
        # Show the dialog and handle response
        file_dialog.open(self.window, None, self._on_import_bibtex_response)
    
    def _on_import_bibtex_response(self, dialog, result):
        """Handle response from import BibTeX dialog.
        
        Args:
            dialog: The dialog
            result: The result
        """
        try:
            file = dialog.open_finish(result)
            if file:
                filepath = file.get_path()
                imported_entries = self.parse_bib_file(filepath)
                
                if not imported_entries:
                    # Show error message
                    message = Adw.MessageDialog.new(
                        self.window,
                        "Import Error",
                        "No valid BibTeX entries were found in the file."
                    )
                    message.add_response("ok", "OK")
                    message.present()
                    return
                
                # If we already have a bibliography file, confirm merge
                if self.current_bib_file:
                    message = Adw.MessageDialog.new(
                        self.window,
                        "Merge Bibliography Files?",
                        f"Do you want to merge {len(imported_entries)} entries from '{os.path.basename(filepath)}' "
                        f"into your current bibliography file '{os.path.basename(self.current_bib_file)}'?"
                    )
                    message.add_response("cancel", "Cancel")
                    message.add_response("merge", "Merge Entries")
                    message.set_default_response("merge")
                    
                    # Connect response handler
                    message.connect("response", self._on_merge_bibliography_response, imported_entries)
                    message.present()
                else:
                    # No current file, just load the imported one
                    self._load_entries_from_file(filepath)
                    
                    # Show the bibliography manager
                    self.show_bibliography_manager()
                    
                    # Update status
                    self.window.update_status(f"Imported {len(imported_entries)} bibliography entries")
        
        except Exception as e:
            print(f"Error importing BibTeX file: {e}")
            self.window.update_status("Error importing BibTeX file")
    
    def _on_merge_bibliography_response(self, dialog, response, imported_entries):
        """Handle response from merge bibliography dialog.
        
        Args:
            dialog: The dialog
            response: The response
            imported_entries: List of imported entries
        """
        if response == "merge":
            # Check for duplicate keys
            existing_keys = {entry['key']: i for i, entry in enumerate(self.entries)}
            duplicates = []
            new_entries = []
            
            for entry in imported_entries:
                if entry['key'] in existing_keys:
                    duplicates.append(entry)
                else:
                    new_entries.append(entry)
            
            # If there are duplicates, ask what to do
            if duplicates:
                message = Adw.MessageDialog.new(
                    self.window,
                    "Duplicate Entries",
                    f"Found {len(duplicates)} duplicate entries. What would you like to do?"
                )
                message.add_response("skip", "Skip Duplicates")
                message.add_response("replace", "Replace Duplicates")
                message.add_response("rename", "Rename Duplicates")
                message.set_default_response("skip")
                
                # Connect response handler
                message.connect("response", self._on_duplicate_entries_response, 
                              duplicates, new_entries)
                message.present()
            else:
                # No duplicates, just add the new entries
                self.entries.extend(new_entries)
                self._save_entries_to_file()
                self._load_entries_from_file(self.current_bib_file)
                
                # Update status
                self.window.update_status(f"Added {len(new_entries)} bibliography entries")
        
        dialog.destroy()
            
            def insert_bibliography_command(self):
        """Insert bibliography command at the current cursor position."""
        if not self.window.editor:
            return
        
        buffer = self.window.editor.buffer
        
        # Create dialog
        dialog = Adw.Dialog()
        dialog.set_title("Insert Bibliography")
        dialog.set_modal(True)
        dialog.set_transient_for(self.window)
        
        # Create main content box
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        
        # Create header bar
        header = Adw.HeaderBar()
        
        # Add cancel button
        cancel_button = Gtk.Button.new_with_label("Cancel")
        cancel_button.connect("clicked", lambda btn: dialog.close())
        header.pack_start(cancel_button)
        
        # Add insert button
        insert_button = Gtk.Button.new_with_label("Insert")
        insert_button.add_css_class("suggested-action")
        header.pack_end(insert_button)
        
        content_box.append(header)
        
        # Create form box
        form_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        form_box.set_margin_top(24)
        form_box.set_margin_bottom(24)
        form_box.set_margin_start(24)
        form_box.set_margin_end(24)
        form_box.set_spacing(12)
        
        # Bibliography style
        style_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        style_label = Gtk.Label(label="Bibliography Style:")
        style_label.set_halign(Gtk.Align.START)
        style_label.set_valign(Gtk.Align.CENTER)
        style_box.append(style_label)
        
        style_combo = Gtk.DropDown()
        string_list = Gtk.StringList()
        for style in ["plain", "unsrt", "alpha", "abbrv", "ieeetr", "acm", "apalike", "siam"]:
            string_list.append(style)
        
        style_combo.set_model(string_list)
        style_combo.set_selected(0)
        style_combo.set_hexpand(True)
        style_box.append(style_combo)
        
        form_box.append(style_box)
        
        # Bibliography file
        file_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        file_label = Gtk.Label(label="Bibliography File:")
        file_label.set_halign(Gtk.Align.START)
        file_label.set_valign(Gtk.Align.CENTER)
        file_box.append(file_label)
        
        file_entry = Gtk.Entry()
        if self.current_bib_file:
            basename = os.path.splitext(os.path.basename(self.current_bib_file))[0]
            file_entry.set_text(basename)
        else:
            file_entry.set_text("bibliography")
        file_entry.set_hexpand(True)
        file_box.append(file_entry)
        
        form_box.append(file_box)
        
        # Command type
        command_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        command_label = Gtk.Label(label="Command Type:")
        command_label.set_halign(Gtk.Align.START)
        command_box.append(command_label)
        
        # Radio buttons for command type
        biblatex_radio = Gtk.CheckButton.new_with_label("Use BibLaTeX (\\addbibresource)")
        bibtex_radio = Gtk.CheckButton.new_with_label("Use BibTeX (\\bibliography)")
        bibtex_radio.set_group(biblatex_radio)
        bibtex_radio.set_active(True)
        
        command_box.append(bibtex_radio)
        command_box.append(biblatex_radio)
        
        form_box.append(command_box)
        
        # Section title options
        title_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        title_label = Gtk.Label(label="Bibliography Title:")
        title_label.set_halign(Gtk.Align.START)
        title_box.append(title_label)
        
        # Radio buttons for title handling
        default_radio = Gtk.CheckButton.new_with_label("Use Default Title")
        custom_radio = Gtk.CheckButton.new_with_label("Use Custom Title")
        none_radio = Gtk.CheckButton.new_with_label("No Title (Manual)")
        
        custom_radio.set_group(default_radio)
        none_radio.set_group(default_radio)
        default_radio.set_active(True)
        
        title_box.append(default_radio)
        title_box.append(custom_radio)
        title_box.append(none_radio)
        
        # Custom title entry
        title_entry = Gtk.Entry()
        title_entry.set_text("References")
        title_entry.set_sensitive(False)
        title_box.append(title_entry)
        
        # Connect custom title radio button to enable/disable the entry
        custom_radio.connect("toggled", lambda btn: title_entry.set_sensitive(btn.get_active()))
        
        form_box.append(title_box)
        
        # Include document preamble options
        preamble_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        preamble_check = Gtk.CheckButton.new_with_label("Include Required Preamble")
        preamble_check.set_active(True)
        preamble_box.append(preamble_check)
        
        form_box.append(preamble_box)
        
        content_box.append(form_box)
        
        # Set dialog content
        dialog.set_content(content_box)
        
        # Connect insert button
        insert_button.connect("clicked", self._on_insert_bibliography, dialog, 
                           style_combo, file_entry, biblatex_radio, 
                           default_radio, custom_radio, none_radio, 
                           title_entry, preamble_check)
        
        # Present dialog
        dialog.present()
            
            def _on_insert_bibliography(self, button, dialog, style_combo, file_entry, 
                               biblatex_radio, default_radio, custom_radio, 
                               none_radio, title_entry, preamble_check):
        """Handle insert bibliography button click."""
        # Get values from dialog
        style_model = style_combo.get_model()
        style = style_model.get_string(style_combo.get_selected())
        
        filename = file_entry.get_text().strip()
        if not filename:
            filename = "bibliography"
        
        use_biblatex = biblatex_radio.get_active()
        use_custom_title = custom_radio.get_active()
        use_no_title = none_radio.get_active()
        custom_title = title_entry.get_text() if use_custom_title else "References"
        include_preamble = preamble_check.get_active()
        
        # Generate the commands
        commands = []
        
        # Add preamble commands if requested
        if include_preamble:
            if use_biblatex:
                commands.append("% Bibliography packages")
                commands.append("\\usepackage[backend=biber]{biblatex}")
                commands.append(f"\\addbibresource{{{filename}.bib}}")
                commands.append("")
            else:
                commands.append("% Bibliography packages")
                commands.append("\\usepackage{natbib}")
                commands.append("")
        
        # Add bibliography command
        commands.append("% Bibliography")
        if use_biblatex:
            if use_no_title:
                commands.append("\\printbibliography")
            elif use_custom_title:
                commands.append(f"\\printbibliography[title={{{custom_title}}}]")
            else:
                commands.append("\\printbibliography")
        else:
            if use_custom_title:
                commands.append(f"\\renewcommand{{\\bibname}}{{{custom_title}}}")
            
            commands.append(f"\\bibliographystyle{{{style}}}")
            commands.append(f"\\bibliography{{{filename}}}")
        
        # Insert into document
        buffer = self.window.editor.buffer
        buffer.begin_user_action()
        
        cursor = buffer.get_insert()
        iter = buffer.get_iter_at_mark(cursor)
        
        # Insert commands
        buffer.insert(iter, "\n".join(commands))
        
        buffer.end_user_action()
        
        # Close dialog
        dialog.close()
        
        # Show notification
        self.window.update_status("Bibliography commands inserted")
    
    def _on_duplicate_entries_response(self, dialog, response, duplicates, new_entries):
        """Handle response from duplicate entries dialog.
        
        Args:
            dialog: The dialog
            response: The response
            duplicates: List of duplicate entries
            new_entries: List of new entries without duplicates
        """
        if response == "skip":
            # Just add the new entries, skip duplicates
            self.entries.extend(new_entries)
            self._save_entries_to_file()
            self._load_entries_from_file(self.current_bib_file)
            
            # Update status
            self.window.update_status(f"Added {len(new_entries)} bibliography entries, skipped {len(duplicates)} duplicates")
            
        elif response == "replace":
            # Replace existing entries with duplicates
            existing_keys = {entry['key']: i for i, entry in enumerate(self.entries)}
            
            for entry in duplicates:
                # Replace the existing entry
                index = existing_keys[entry['key']]
                self.entries[index] = entry
            
            # Add the new entries
            self.entries.extend(new_entries)
            self._save_entries_to_file()
            self._load_entries_from_file(self.current_bib_file)
            
            # Update status
            self.window.update_status(f"Added {len(new_entries)} new entries, replaced {len(duplicates)} duplicates")
            
        elif response == "rename":
            # Rename duplicate entries by adding a suffix
            existing_keys = {entry['key']: i for i, entry in enumerate(self.entries)}
            
            for entry in duplicates:
                # Generate a new unique key
                base_key = entry['key']
                suffix = 1
                new_key = f"{base_key}_{suffix}"
                
                # Keep incrementing suffix until we find a unique key
                while new_key in existing_keys or any(e['key'] == new_key for e in new_entries):
                    suffix += 1
                    new_key = f"{base_key}_{suffix}"
                
                # Rename the entry
                entry['key'] = new_key
                new_entries.append(entry)
            
            # Add all the entries (renamed and new)
            self.entries.extend(new_entries)
            self._save_entries_to_file()
            self._load_entries_from_file(self.current_bib_file)
            
            # Update status
            self.window.update_status(f"Added {len(new_entries)} entries, renamed {len(duplicates)} duplicates")
        
        dialog.destroy()
        
        # Show the bibliography manager
        self.show_bibliography_manager()
