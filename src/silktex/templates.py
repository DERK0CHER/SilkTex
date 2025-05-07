    def on_select_clicked(self, button):
        """Handle select button click"""
        if self.selected_template:
            template_content = self.template_manager.get_template(self.selected_template)
            if template_content:
                # Emit a custom signal with the template content
                self.emit('template-selected', template_content)
                self.close()


class TemplateEditorDialog(Adw.Dialog):
    """Dialog for creating or editing templates"""
    
    __gsignals__ = {
        'response': (GObject.SignalFlags.RUN_LAST, None, (str,)),
    }
    
    def __init__(self, parent_window, template_manager, template_id=None, 
                 template_name='', template_description='', template_content=''):
        """Initialize the template editor dialog
        
        Args:
            parent_window: Parent window
            template_manager: Template manager instance
            template_id: Template ID if editing an existing template (optional)
            template_name: Template name (optional)
            template_description: Template description (optional)
            template_content: Template content (optional)
        """
        super().__init__()
        
        self.parent_window = parent_window
        self.template_manager = template_manager
        self.template_id = template_id
        
        # Set up dialog properties
        if template_id:
            self.set_title("Edit Template")
        else:
            self.set_title("New Template")
            
        self.set_content_width(800)
        self.set_content_height(600)
        
        # Initialize with template data
        self.template_name = template_name
        self.template_description = template_description
        self.template_content = template_content
        
        # Create UI
        self.create_ui()
    
    def create_ui(self):
        """Create the dialog UI"""
        # Main content box
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        content_box.set_margin_top(24)
        content_box.set_margin_bottom(24)
        content_box.set_margin_start(24)
        content_box.set_margin_end(24)
        content_box.set_spacing(24)
        
        # Template metadata section
        metadata_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        metadata_box.set_spacing(12)
        
        # Template name
        name_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        name_box.set_spacing(12)
        
        name_label = Gtk.Label.new("Name:")
        name_label.set_halign(Gtk.Align.START)
        name_label.set_valign(Gtk.Align.CENTER)
        name_label.set_width_chars(12)
        name_box.append(name_label)
        
        self.name_entry = Gtk.Entry()
        self.name_entry.set_text(self.template_name)
        self.name_entry.set_hexpand(True)
        name_box.append(self.name_entry)
        
        metadata_box.append(name_box)
        
        # Template description
        desc_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        desc_box.set_spacing(12)
        
        desc_label = Gtk.Label.new("Description:")
        desc_label.set_halign(Gtk.Align.START)
        desc_label.set_valign(Gtk.Align.CENTER)
        desc_label.set_width_chars(12)
        desc_box.append(desc_label)
        
        self.desc_entry = Gtk.Entry()
        self.desc_entry.set_text(self.template_description)
        self.desc_entry.set_hexpand(True)
        desc_box.append(self.desc_entry)
        
        metadata_box.append(desc_box)
        
        content_box.append(metadata_box)
        
        # Create a separator
        separator = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        content_box.append(separator)
        
        # Template content editor
        editor_label = Gtk.Label.new("Template Content:")
        editor_label.set_halign(Gtk.Align.START)
        content_box.append(editor_label)
        
        # Add scrolled window for editor
        scrolled_window = Gtk.ScrolledWindow()
        scrolled_window.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scrolled_window.set_vexpand(True)
        
        # Try to use GtkSourceView if available, otherwise use a regular TextView
        try:
            gi.require_version('GtkSource', '5')
            from gi.repository import GtkSource
            
            # Create source buffer with LaTeX highlighting
            lang_manager = GtkSource.LanguageManager.get_default()
            latex_lang = lang_manager.get_language("latex")
            
            buffer = GtkSource.Buffer()
            if latex_lang:
                buffer.set_language(latex_lang)
            buffer.set_text(self.template_content)
            
            # Create source view
            self.editor = GtkSource.View(buffer=buffer)
            self.editor.set_show_line_numbers(True)
            self.editor.set_tab_width(2)
            self.editor.set_auto_indent(True)
            self.editor.set_indent_width(2)
            self.editor.set_wrap_mode(Gtk.WrapMode.WORD)
        except (ImportError, ValueError):
            # Fall back to regular TextView
            self.editor = Gtk.TextView()
            self.editor.set_wrap_mode(Gtk.WrapMode.WORD)
            buffer = self.editor.get_buffer()
            buffer.set_text(self.template_content)
        
        scrolled_window.set_child(self.editor)
        content_box.append(scrolled_window)
        
        # Action buttons
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_halign(Gtk.Align.END)
        button_box.set_spacing(12)
        
        cancel_button = Gtk.Button.new_with_label("Cancel")
        cancel_button.connect('clicked', self.on_cancel_clicked)
        button_box.append(cancel_button)
        
        save_button = Gtk.Button.new_with_label("Save Template")
        save_button.add_css_class('suggested-action')
        save_button.connect('clicked', self.on_save_clicked)
        button_box.append(save_button)
        
        content_box.append(button_box)
        
        # Set dialog content
        self.set_content(content_box)
    
    def on_cancel_clicked(self, button):
        """Handle cancel button click"""
        self.close()
    
    def on_save_clicked(self, button):
        """Handle save button click"""
        # Get template data from UI
        name = self.name_entry.get_text().strip()
        description = self.desc_entry.get_text().strip()
        
        # Get text from buffer
        buffer = self.editor.get_buffer()
        start = buffer.get_start_iter()
        end = buffer.get_end_iter()
        content = buffer.get_text(start, end, False)
        
        # Validate input
        if not name:
            dialog = Adw.MessageDialog.new(
                self.parent_window,
                "Invalid Template",
                "Please enter a name for the template."
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return
        
        if not content.strip():
            dialog = Adw.MessageDialog.new(
                self.parent_window,
                "Invalid Template",
                "Template content cannot be empty."
            )
            dialog.add_response("ok", "OK")
            dialog.present()
            return
            
        # Save the template
        if self.template_manager.save_template(name, description, content):
            # Signal success
            self.emit('response', 'save')
            self.close()
        else:
            # Show error dialog
            dialog = Adw.MessageDialog.new(
                self.parent_window,
                "Template Save Error",
                "Failed to save the template. Please try again."
            )
            dialog.add_response("ok", "OK")
            dialog.present()


# Add GObject registration for custom signals
GObject.type_register(TemplateDialog)
GObject.signal_new('template-selected', TemplateDialog, GObject.SignalFlags.RUN_LAST,
                   GObject.TYPE_NONE, (GObject.TYPE_STRING,))

GObject.type_register(TemplateEditorDialog)
