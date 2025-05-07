        category_dropdown.set_model(category_model)
        category_box.append(category_dropdown)
        
        content_box.append(category_box)
        
        # Search entry
        search_entry = Gtk.SearchEntry()
        search_entry.set_placeholder_text("Search snippets...")
        content_box.append(search_entry)
        
        # Snippets ListView
        list_box = Gtk.ListBox()
        list_box.set_selection_mode(Gtk.SelectionMode.SINGLE)
        list_box.set_hexpand(True)
        list_box.set_vexpand(True)
        list_box.add_css_class("boxed-list")
        
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scrolled.set_min_content_height(300)
        scrolled.set_vexpand(True)
        scrolled.set_child(list_box)
        content_box.append(scrolled)
        
        # Snippet preview
        preview_frame = Gtk.Frame()
        preview_frame.set_label("Snippet Preview")
        
        preview_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        preview_box.set_margin_top(8)
        preview_box.set_margin_bottom(8)
        preview_box.set_margin_start(8)
        preview_box.set_margin_end(8)
        
        preview_text = GtkSource.View()
        preview_text.set_monospace(True)
        preview_text.set_editable(False)
        preview_text.set_wrap_mode(Gtk.WrapMode.WORD)
        preview_text.set_vexpand(True)
        
        # Set up buffer with LaTeX syntax highlighting
        language_manager = GtkSource.LanguageManager.get_default()
        preview_buffer = GtkSource.Buffer.new_with_language(language_manager.get_language('latex'))
        preview_text.set_buffer(preview_buffer)
        
        preview_box.append(preview_text)
        preview_frame.set_child(preview_box)
        content_box.append(preview_frame)
        
        # Buttons
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_spacing(8)
        button_box.set_halign(Gtk.Align.END)
        content_box.append(button_box)
        
        cancel_button = Gtk.Button(label="Cancel")
        cancel_button.connect("clicked", lambda _: dialog.destroy())
        button_box.append(cancel_button)
        
        insert_button = Gtk.Button(label="Insert Snippet")
        insert_button.add_css_class("suggested-action")
        button_box.append(insert_button)
        
        # Functions to populate the list
        def populate_snippets(category=None, search_text=None):
            # Clear the list
            while True:
                row = list_box.get_first_child()
                if row is None:
                    break
                list_box.remove(row)
            
            # Get snippets to display
            snippets_to_show = []
            if category == "All" or category is None:
                snippets_to_show = self.get_all_snippets()
            else:
                snippets_to_show = self.get_snippets_by_category(category)
            
            # Filter by search text if provided
            if search_text:
                search_text = search_text.lower()
                snippets_to_show = [s for s in snippets_to_show if 
                                   search_text in s['name'].lower() or 
                                   search_text in s['description'].lower() or
                                   ('shortcut' in s and search_text in s['shortcut'].lower())]
            
            # Sort snippets by name
            snippets_to_show.sort(key=lambda s: s['name'])
            
            # Add snippets to the list
            for snippet in snippets_to_show:
                row = Adw.ActionRow()
                row.set_title(snippet['name'])
                row.set_subtitle(snippet['description'])
                
                # Show shortcut if available
                if 'shortcut' in snippet and snippet['shortcut']:
                    shortcut_label = Gtk.Label(label=f"#{snippet['shortcut']}")
                    shortcut_label.add_css_class("dim-label")
                    shortcut_label.add_css_class("caption")
                    row.add_suffix(shortcut_label)
                
                # Store snippet ID
                row.set_data('snippet-id', snippet['id'])
                
                list_box.append(row)
        
        # Initialize with all snippets
        populate_snippets()
        
        # Connect selection signal
        def on_row_activated(list_box, row):
            # Enable insert button when a row is selected
            insert_button.set_sensitive(True)
            
            # Show snippet preview
            snippet_id = row.get_data('snippet-id')
            snippet = self.get_snippet_by_id(snippet_id)
            if snippet:
                preview_buffer.set_text(snippet['content'])
        
        list_box.connect("row-activated", on_row_activated)
        
        # Connect category dropdown signal
        def on_category_changed(dropdown, _):
            selected = dropdown.get_selected()
            category = all_categories[selected]
            populate_snippets(category, search_entry.get_text())
        
        category_dropdown.connect("notify::selected", on_category_changed)
        
        # Connect search entry signal
        def on_search_changed(entry):
            selected = category_dropdown.get_selected()
            category = all_categories[selected]
            populate_snippets(category, entry.get_text())
        
        search_entry.connect("search-changed", on_search_changed)
        
        # Connect insert button
        def on_insert_clicked(button):
            selected_row = list_box.get_selected_row()
            if selected_row:
                snippet_id = selected_row.get_data('snippet-id')
                snippet = self.get_snippet_by_id(snippet_id)
                
                if snippet and on_snippet_selected:
                    on_snippet_selected(snippet)
            
            dialog.destroy()
        
        insert_button.connect("clicked", on_insert_clicked)
        insert_button.set_sensitive(False)
        
        # Show dialog
        dialog.set_content(content_box)
        dialog.present()
