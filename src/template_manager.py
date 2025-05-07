# template_manager.py - Template management for LaTeX documents
import os
import json
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, Adw, GLib, Gio


class TemplateManager:
    """Manages LaTeX document templates"""
    
    def __init__(self, config):
        """Initialize template manager"""
        self.config = config
        self.templates = {}
        self.default_template_id = "basic_article"
        
        # Load built-in templates
        self.load_built_in_templates()
        
        # Load user templates
        self.load_user_templates()
    
    def get_template_dir(self):
        """Get the user template directory"""
        template_dir = os.path.join(GLib.get_user_data_dir(), 'silktex', 'templates')
        
        # Create directory if it doesn't exist
        if not os.path.exists(template_dir):
            os.makedirs(template_dir)
        
        return template_dir
    
    def load_built_in_templates(self):
        """Load built-in templates"""
        # Basic article template
        basic_article = {
            'id': 'basic_article',
            'name': 'Basic Article',
            'description': 'A basic LaTeX article template',
            'content': (
                "\\documentclass[12pt,a4paper]{article}\n"
                "\\usepackage[utf8]{inputenc}\n"
                "\\usepackage[T1]{fontenc}\n"
                "\\usepackage{amsmath}\n"
                "\\usepackage{amsfonts}\n"
                "\\usepackage{amssymb}\n"
                "\\usepackage{graphicx}\n"
                "\\usepackage[colorlinks=true, linkcolor=blue, urlcolor=blue, citecolor=blue]{hyperref}\n\n"
                "\\title{Document Title}\n"
                "\\author{Author Name}\n"
                "\\date{\\today}\n\n"
                "\\begin{document}\n\n"
                "\\maketitle\n\n"
                "\\section{Introduction}\n"
                "This is a basic LaTeX document template.\n\n"
                "\\end{document}"
            )
        }
        
        # Report template
        report_template = {
            'id': 'report',
            'name': 'Report',
            'description': 'A template for longer reports with chapters',
            'content': (
                "\\documentclass[12pt,a4paper]{report}\n"
                "\\usepackage[utf8]{inputenc}\n"
                "\\usepackage[T1]{fontenc}\n"
                "\\usepackage{amsmath}\n"
                "\\usepackage{amsfonts}\n"
                "\\usepackage{amssymb}\n"
                "\\usepackage{graphicx}\n"
                "\\usepackage[colorlinks=true, linkcolor=blue, urlcolor=blue, citecolor=blue]{hyperref}\n\n"
                "\\title{Report Title}\n"
                "\\author{Author Name}\n"
                "\\date{\\today}\n\n"
                "\\begin{document}\n\n"
                "\\maketitle\n"
                "\\tableofcontents\n\n"
                "\\chapter{Introduction}\n"
                "This is the introduction chapter of the report.\n\n"
                "\\chapter{Background}\n"
                "This chapter provides background information.\n\n"
                "\\chapter{Methodology}\n"
                "This chapter describes the methodology.\n\n"
                "\\chapter{Results}\n"
                "This chapter presents the results.\n\n"
                "\\chapter{Conclusion}\n"
                "This chapter concludes the report.\n\n"
                "\\end{document}"
            )
        }
        
        # Beamer presentation template
        beamer_template = {
            'id': 'presentation',
            'name': 'Beamer Presentation',
            'description': 'A template for creating presentations with Beamer',
            'content': (
                "\\documentclass{beamer}\n"
                "\\usepackage[utf8]{inputenc}\n"
                "\\usepackage[T1]{fontenc}\n"
                "\\usepackage{amsmath}\n"
                "\\usepackage{amsfonts}\n"
                "\\usepackage{amssymb}\n"
                "\\usepackage{graphicx}\n\n"
                "\\usetheme{Madrid}\n"
                "\\usecolortheme{default}\n\n"
                "\\title{Presentation Title}\n"
                "\\author{Presenter Name}\n"
                "\\institute{Institution Name}\n"
                "\\date{\\today}\n\n"
                "\\begin{document}\n\n"
                "\\begin{frame}\n"
                "\\titlepage\n"
                "\\end{frame}\n\n"
                "\\begin{frame}{Outline}\n"
                "\\tableofcontents\n"
                "\\end{frame}\n\n"
                "\\section{Introduction}\n\n"
                "\\begin{frame}{Introduction}\n"
                "\\begin{itemize}\n"
                "\\item First point\n"
                "\\item Second point\n"
                "\\item Third point\n"
                "\\end{itemize}\n"
                "\\end{frame}\n\n"
                "\\section{Content}\n\n"
                "\\begin{frame}{Content}\n"
                "Content goes here.\n"
                "\\end{frame}\n\n"
                "\\section{Conclusion}\n\n"
                "\\begin{frame}{Conclusion}\n"
                "Conclusion goes here.\n"
                "\\end{frame}\n\n"
                "\\end{document}"
            )
        }
        
        # Letter template
        letter_template = {
            'id': 'letter',
            'name': 'Letter',
            'description': 'A template for formal letters',
            'content': (
                "\\documentclass{letter}\n"
                "\\usepackage[utf8]{inputenc}\n"
                "\\usepackage[T1]{fontenc}\n"
                "\\usepackage{graphicx}\n\n"
                "\\signature{Sender Name}\n"
                "\\address{Sender Address Line 1 \\\\ Sender Address Line 2 \\\\ Sender Address Line 3}\n\n"
                "\\begin{document}\n\n"
                "\\begin{letter}{Recipient Name \\\\ Recipient Address Line 1 \\\\ Recipient Address Line 2 \\\\ Recipient Address Line 3}\n\n"
                "\\opening{Dear Recipient,}\n\n"
                "This is the body of the letter.\n\n"
                "\\closing{Sincerely,}\n\n"
                "\\end{letter}\n\n"
                "\\end{document}"
            )
        }
        
        # Add built-in templates to the template list
        self.templates['basic_article'] = basic_article
        self.templates['report'] = report_template
        self.templates['presentation'] = beamer_template
        self.templates['letter'] = letter_template
    
    def load_user_templates(self):
        """Load user-defined templates"""
        template_dir = self.get_template_dir()
        
        # Look for JSON template files
        for filename in os.listdir(template_dir):
            if filename.endswith('.json'):
                try:
                    with open(os.path.join(template_dir, filename), 'r') as f:
                        template = json.load(f)
                    
                    if 'id' in template and 'name' in template and 'content' in template:
                        self.templates[template['id']] = template
                except Exception as e:
                    print(f"Error loading template {filename}: {e}")
    
    def get_templates(self):
        """Get all available templates"""
        return self.templates.values()
    
    def get_template(self, template_id):
        """Get a specific template by ID"""
        return self.templates.get(template_id, None)
    
    def get_default_template(self):
        """Get the default template content"""
        default_template = self.templates.get(self.default_template_id)
        if default_template:
            return default_template['content']
        
        # Fallback to a very simple template
        return (
            "\\documentclass[12pt,a4paper]{article}\n"
            "\\usepackage[utf8]{inputenc}\n"
            "\\begin{document}\n\n"
            "% Your content here\n\n"
            "\\end{document}"
        )
    
    def save_as_template(self, name, description, content):
        """Save the current document as a template"""
        # Generate a simple ID from the name
        template_id = name.lower().replace(' ', '_')
        
        # Make sure ID is unique
        if template_id in self.templates:
            base_id = template_id
            counter = 1
            while template_id in self.templates:
                template_id = f"{base_id}_{counter}"
                counter += 1
        
        # Create template object
        template = {
            'id': template_id,
            'name': name,
            'description': description,
            'content': content
        }
        
        # Save to templates dictionary
        self.templates[template_id] = template
        
        # Save to file
        template_dir = self.get_template_dir()
        try:
            with open(os.path.join(template_dir, f"{template_id}.json"), 'w') as f:
                json.dump(template, f, indent=2)
            return True
        except Exception as e:
            print(f"Error saving template: {e}")
            return False
    
    def delete_template(self, template_id):
        """Delete a user-defined template"""
        if template_id not in self.templates:
            return False
        
        # Built-in templates can't be deleted
        if template_id in ['basic_article', 'report', 'presentation', 'letter']:
            return False
        
        # Remove from dictionary
        template = self.templates.pop(template_id)
        
        # Delete the file
        template_dir = self.get_template_dir()
        try:
            template_file = os.path.join(template_dir, f"{template_id}.json")
            if os.path.exists(template_file):
                os.remove(template_file)
            return True
        except Exception as e:
            # Re-add template in case of error
            self.templates[template_id] = template
            print(f"Error deleting template: {e}")
            return False
    
    def show_template_dialog(self, parent_window, on_template_selected=None):
        """Show a dialog for selecting a template"""
        dialog = Adw.Window()
        dialog.set_title("Select Template")
        dialog.set_default_size(500, 600)
        dialog.set_transient_for(parent_window)
        dialog.set_modal(True)
        
        # Create dialog content
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        content_box.set_spacing(12)
        content_box.set_margin_top(12)
        content_box.set_margin_bottom(12)
        content_box.set_margin_start(12)
        content_box.set_margin_end(12)
        
        # Header
        header = Adw.HeaderBar()
        content_box.append(header)
        
        # Template ListView
        list_box = Gtk.ListBox()
        list_box.set_selection_mode(Gtk.SelectionMode.SINGLE)
        list_box.set_hexpand(True)
        list_box.set_vexpand(True)
        list_box.add_css_class("boxed-list")
        
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scrolled.set_min_content_height(400)
        scrolled.set_vexpand(True)
        scrolled.set_child(list_box)
        content_box.append(scrolled)
        
        # Sort templates by name
        sorted_templates = sorted(self.templates.values(), key=lambda t: t['name'])
        
        # Add templates to the list
        for template in sorted_templates:
            row = Adw.ActionRow()
            row.set_title(template['name'])
            row.set_subtitle(template['description'])
            
            # Store template ID
            row.set_data('template-id', template['id'])
            
            list_box.append(row)
        
        # Buttons
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_spacing(8)
        button_box.set_halign(Gtk.Align.END)
        content_box.append(button_box)
        
        cancel_button = Gtk.Button(label="Cancel")
        cancel_button.connect("clicked", lambda _: dialog.destroy())
        button_box.append(cancel_button)
        
        select_button = Gtk.Button(label="Select Template")
        select_button.add_css_class("suggested-action")
        button_box.append(select_button)
        
        # Connect selection signal
        def on_row_activated(list_box, row):
            # Enable select button when a row is selected
            select_button.set_sensitive(True)
        
        list_box.connect("row-activated", on_row_activated)
        
        def on_select_clicked(button):
            selected_row = list_box.get_selected_row()
            if selected_row:
                template_id = selected_row.get_data('template-id')
                template = self.get_template(template_id)
                
                if template and on_template_selected:
                    on_template_selected(template)
            
            dialog.destroy()
        
        select_button.connect("clicked", on_select_clicked)
        select_button.set_sensitive(False)
        
        # Show dialog
        dialog.set_content(content_box)
        dialog.present()
