import sys
import os
import gi
import tempfile
import subprocess
import re
import threading
import time
import json
from pathlib import Path

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('GtkSource', '5')
gi.require_version('WebKit', '6.0')

from gi.repository import Gtk, Gio, Adw, GLib, GtkSource, WebKit, Gdk, Pango, GObject


class LaTeXSnippets:
    def __init__(self):
        # Default snippets that will be used if no custom snippets are found
        self.default_snippets = {
            # Document structure
            "\\begin": "\\begin{$1}\n\t$0\n\\end{$1}",
            "\\section": "\\section{$1}\n$0",
            "\\subsection": "\\subsection{$1}\n$0",
            "\\subsubsection": "\\subsubsection{$1}\n$0",
            "\\chapter": "\\chapter{$1}\n$0",
            "\\paragraph": "\\paragraph{$1} $0",
            "\\newpage": "\\newpage\n$0",
            "\\tableofcontents": "\\tableofcontents\n$0",
            
            # Environments
            "\\document": "\\begin{document}\n$0\n\\end{document}",
            "\\figure": "\\begin{figure}[${1:htbp}]\n\t\\centering\n\t\\includegraphics[width=${2:0.8}\\textwidth]{${3:filename}}\n\t\\caption{${4:caption}}\n\t\\label{fig:${5:label}}\n\\end{figure}",
            "\\table": "\\begin{table}[${1:htbp}]\n\t\\centering\n\t\\begin{tabular}{${2:c c c}}\n\t\t${3:header1} & ${4:header2} & ${5:header3} \\\\\n\t\t\\hline\n\t\t${6:cell1} & ${7:cell2} & ${8:cell3} \\\\\n\t\t${0:cell4} & ${0:cell5} & ${0:cell6} \\\\\n\t\\end{tabular}\n\t\\caption{${9:caption}}\n\t\\label{tab:${10:label}}\n\\end{table}",
            "\\equation": "\\begin{equation}\n\t$0\n\\end{equation}",
            "\\align": "\\begin{align}\n\t$0\n\\end{align}",
            "\\itemize": "\\begin{itemize}\n\t\\item $0\n\\end{itemize}",
            "\\enumerate": "\\begin{enumerate}\n\t\\item $0\n\\end{enumerate}",
            "\\description": "\\begin{description}\n\t\\item[$1] $0\n\\end{description}",
            "\\verbatim": "\\begin{verbatim}\n$0\n\\end{verbatim}",
            "\\center": "\\begin{center}\n\t$0\n\\end{center}",
            "\\quote": "\\begin{quote}\n\t$0\n\\end{quote}",
            "\\abstract": "\\begin{abstract}\n\t$0\n\\end{abstract}",
            
            # Math
            "\\frac": "\\frac{$1}{$2}$0",
            "\\sum": "\\sum_{$1}^{$2} $0",
            "\\int": "\\int_{$1}^{$2} $0",
            "\\sqrt": "\\sqrt{$1}$0",
            "\\matrix": "\\begin{matrix}\n\t$1 & $2 \\\\\n\t$3 & $4\n\\end{matrix}$0",
            "\\lim": "\\lim_{$1 \\to $2} $0",
            
            # Citations and references
            "\\cite": "\\cite{$1}$0",
            "\\ref": "\\ref{$1}$0",
            "\\label": "\\label{$1}$0",
            "\\footnote": "\\footnote{$1}$0",
            "\\bibliographystyle": "\\bibliographystyle{${1:plain}}\n\\bibliography{${2:references}}$0",
            
            # Formatting
            "\\textbf": "\\textbf{$1}$0",
            "\\textit": "\\textit{$1}$0",
            "\\texttt": "\\texttt{$1}$0",
            "\\underline": "\\underline{$1}$0",
            "\\emph": "\\emph{$1}$0",
            "\\textcolor": "\\textcolor{$1}{$2}$0",
            
            # Document preamble
            "\\documentclass": "\\documentclass[${1:11pt}]{${2:article}}\n$0",
            "\\usepackage": "\\usepackage{$1}$0",
            "\\title": "\\title{$1}$0",
            "\\author": "\\author{$1}$0",
            "\\date": "\\date{$1}$0",
            "\\maketitle": "\\maketitle\n$0",
            
            # Templates
            "article": "\\documentclass[11pt]{article}\n\\usepackage{amsmath}\n\\usepackage{graphicx}\n\\usepackage{hyperref}\n\\usepackage[utf8]{inputenc}\n\\usepackage[english]{babel}\n\n\\title{${1:Title}}\n\\author{${2:Author}}\n\\date{\\today}\n\n\\begin{document}\n\n\\maketitle\n\n\\begin{abstract}\n${3:Abstract}\n\\end{abstract}\n\n\\section{Introduction}\n$0\n\n\\section{Conclusion}\n\n\\bibliographystyle{plain}\n\\bibliography{references}\n\n\\end{document}",
            "beamer": "\\documentclass{beamer}\n\\usepackage{amsmath}\n\\usepackage{graphicx}\n\\usepackage{hyperref}\n\\usepackage[utf8]{inputenc}\n\n\\title{${1:Presentation Title}}\n\\author{${2:Author}}\n\\date{\\today}\n\n\\begin{document}\n\n\\frame{\\titlepage}\n\n\\begin{frame}\n\\frametitle{${3:First Frame}}\n$0\n\\end{frame}\n\n\\end{document}",
            "letter": "\\documentclass{letter}\n\\usepackage[utf8]{inputenc}\n\\address{${1:Sender's Address}}\n\\signature{${2:Sender's Name}}\n\n\\begin{document}\n\n\\begin{letter}{${3:Recipient's Address}}\n\n\\opening{${4:Dear Sir or Madam,}}\n\n$0\n\n\\closing{${5:Yours sincerely,}}\n\n\\end{letter}\n\n\\end{document}"
        }
        
        # Initialize with default snippets
        self.snippets = self.default_snippets.copy()
        
        # Try to load custom snippets
        self.load_snippets()
    
    def load_snippets(self):
        """Load snippets from the user's configuration file"""
        config_dir = os.path.expanduser("~/.config/SilkTex/snippets")
        config_file = os.path.join(config_dir, "snippets.cfg")
        
        # Create directory if it doesn't exist
        if not os.path.exists(config_dir):
            try:
                os.makedirs(config_dir)
                # Save default snippets to create the initial file
                self.save_snippets(self.default_snippets)
            except OSError as e:
                print(f"Error creating config directory: {e}")
                return
        
        # Load snippets if the file exists
        if os.path.exists(config_file):
            try:
                with open(config_file, 'r') as f:
                    # Try to parse the file as JSON
                    try:
                        custom_snippets = json.load(f)
                        # Merge with default snippets, custom snippets take precedence
                        self.snippets.update(custom_snippets)
                    except json.JSONDecodeError:
                        # If JSON parsing fails, try to parse line by line in key=value format
                        f.seek(0)  # Go back to the beginning of the file
                        for line in f:
                            line = line.strip()
                            if line and not line.startswith('#'):
                                try:
                                    key, value = line.split('=', 1)
                                    key = key.strip()
                                    value = value.strip()
                                    if key and value:
                                        # Handle escaped newlines
                                        value = value.replace('\\n', '\n')
                                        value = value.replace('\\t', '\t')
                                        self.snippets[key] = value
                                except ValueError:
                                    # Skip lines that don't have the correct format
                                    continue
            except Exception as e:
                print(f"Error loading snippets: {e}")
    
    def save_snippets(self, snippets=None):
        """Save snippets to the configuration file"""
        if snippets is None:
            snippets = self.snippets
            
        config_dir = os.path.expanduser("~/.config/SilkTex/snippets")
        config_file = os.path.join(config_dir, "snippets.cfg")
        
        try:
            # Make sure the directory exists
            os.makedirs(config_dir, exist_ok=True)
            
            # Save snippets in JSON format
            with open(config_file, 'w') as f:
                json.dump(snippets, f, indent=2)
                
            return True
        except Exception as e:
            print(f"Error saving snippets: {e}")
            return False
    
    def get_snippet(self, trigger):
        """Get snippet content for a trigger"""
        return self.snippets.get(trigger)
    
    def get_all_snippets(self):
        """Get all available snippets"""
        return self.snippets
    
    def get_matching_snippets(self, prefix):
        """Get snippets that match the given prefix"""
        return {k: v for k, v in self.snippets.items() if k.startswith(prefix)}
    
    def add_snippet(self, trigger, content):
        """Add a new snippet or update an existing one"""
        self.snippets[trigger] = content
        return self.save_snippets()
    
    def remove_snippet(self, trigger):
        """Remove a snippet by its trigger"""
        if trigger in self.snippets:
            del self.snippets[trigger]
            return self.save_snippets()
        return False
    
    def reset_to_defaults(self):
        """Reset all snippets to default values"""
        self.snippets = self.default_snippets.copy()
        return self.save_snippets()


class LatexCompletionProvider(GObject.GObject, GtkSource.CompletionProvider):
    __gtype_name__ = 'LatexCompletionProvider'
    
    def __init__(self):
        super().__init__()
        self.snippets = LaTeXSnippets()
    
    def do_get_name(self):
        return "LaTeX"
    
    def do_get_priority(self):
        # Higher priority means it will be shown above other providers
        return 100
    
    def do_get_icon(self):
        # Return a LaTeX icon if available, or None otherwise
        theme = Gtk.IconTheme.get_for_display(Gdk.Display.get_default())
        if theme.has_icon("text-x-tex"):
            return Gtk.Image.new_from_icon_name("text-x-tex").get_paintable()
        return None
    
    def do_populate(self, context):
        iter = context.get_iter()
        buffer = iter.get_buffer()
        
        # Get the text from the beginning of the line to the cursor position
        line_start = iter.copy()
        line_start.set_line_offset(0)
        text = buffer.get_text(line_start, iter, False)
        
        # Find the word being typed (starting with \)
        match = re.search(r'\\([a-zA-Z]*)$', text)
        if not match:
            context.add_proposals(self, [], True)
            return
        
        prefix = '\\' + match.group(1)
        matching_snippets = self.snippets.get_matching_snippets(prefix)
        
        proposals = []
        for trigger, content in matching_snippets.items():
            # Create a display string that shows the snippet structure
            display = trigger + " → " + content.split("\n")[0].replace("$1", "...").replace("$0", "")
            
            # Create a proposal
            proposal = GtkSource.CompletionItem.new()
            proposal.set_label(display)
            proposal.set_text(trigger)
            
            # Add a description if available
            proposal.set_info(content.replace("$1", "...").replace("$0", ""))
            
            proposals.append(proposal)
        
        context.add_proposals(self, proposals, True)
    
    def do_activate(self, context, proposal):
        # Get the active buffer and insert position
        iter = context.get_iter()
        buffer = iter.get_buffer()
        
        # Get the trigger text from the proposal
        trigger = proposal.get_text()
        snippet_content = self.snippets.get_snippet(trigger)
        
        if not snippet_content:
            return False
        
        # Find the start of the command
        line_start = iter.copy()
        line_start.set_line_offset(0)
        text_to_cursor = buffer.get_text(line_start, iter, False)
        match = re.search(r'\\([a-zA-Z]*)$', text_to_cursor)
        
        if not match:
            return False
        
        # Calculate start and end positions
        start_pos = iter.get_offset() - len(match.group(0))
        start_iter = buffer.get_iter_at_offset(start_pos)
        
        # Begin a user action for undo purposes
        buffer.begin_user_action()
        
        # Delete the command text
        buffer.delete(start_iter, iter)
        
        # Insert the snippet content (basic implementation, could be enhanced)
        # This simple version doesn't handle placeholders, just inserts the raw text
        processed_content = snippet_content
        processed_content = re.sub(r'\$\{[0-9]+:([^}]*)\}', r'\1', processed_content)  # Handle ${1:default} format
        processed_content = re.sub(r'\$[0-9]+', '', processed_content)  # Remove $1, $2, etc.
        
        # Insert at current position
        buffer.insert_at_cursor(processed_content)
        
        # End user action
        buffer.end_user_action()
        
        return True


        class DocumentStructure(Gtk.Box):
            def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.set_orientation(Gtk.Orientation.VERTICAL)

        # Create a title
        title = Gtk.Label()
        title.set_markup("<b>Document Structure</b>")
        title.set_halign(Gtk.Align.START)
        title.set_margin_bottom(10)
        title.set_margin_top(10)
        title.set_margin_start(10)
        self.append(title)

        # Create a scrolled window
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)

        # Create a tree view for document structure
        self.structure_store = Gtk.TreeStore(str, str, int)  # Text, Type, Line number
        self.structure_view = Gtk.TreeView(model=self.structure_store)
        self.structure_view.set_headers_visible(False)

        renderer = Gtk.CellRendererText()
        column = Gtk.TreeViewColumn("Section", renderer, text=0)
        self.structure_view.append_column(column)

        # Connect to selection change
        select = self.structure_view.get_selection()
        select.connect("changed", self.on_selection_changed)

        scrolled.set_child(self.structure_view)
        self.append(scrolled)

        # Add a button box at the bottom
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_margin_top(10)
        button_box.set_margin_bottom(10)
        button_box.set_margin_start(10)
        button_box.set_margin_end(10)
        button_box.set_spacing(5)

        refresh_button = Gtk.Button(label="Refresh")
        refresh_button.set_hexpand(True)
        refresh_button.connect("clicked", self.on_refresh_clicked)
        button_box.append(refresh_button)

        self.append(button_box)

        # Store reference to source view
        self.source_view = None

            def set_source_view(self, source_view):
        """Set the source view to navigate to when an item is selected"""
        self.source_view = source_view

            def on_selection_changed(self, selection):
        """Navigate to the selected section in the document"""
        if not self.source_view:
            return

        model, treeiter = selection.get_selected()
        if treeiter is not None:
            line_num = model[treeiter][2]
            if line_num >= 0:
                # Navigate to the line in the source view
                buffer = self.source_view.get_buffer()
                line_iter = buffer.get_iter_at_line(line_num)
                buffer.place_cursor(line_iter)
                self.source_view.scroll_to_iter(line_iter, 0.25, False, 0.0, 0.0)

            def on_refresh_clicked(self, button):
        """Refresh the document structure"""
        if self.source_view:
            buffer = self.source_view.get_buffer()
            start_iter = buffer.get_start_iter()
            end_iter = buffer.get_end_iter()
            text = buffer.get_text(start_iter, end_iter, False)
            self.update_structure(text)

            def update_structure(self, tex_content):
        """Update the document structure based on LaTeX content"""
        self.structure_store.clear()

        # Simple regex-based parsing of LaTeX structure
        lines = tex_content.split('\n')
        section_stack = [None]  # Root
        document_node = None
        preamble_node = None

        for i, line in enumerate(lines):
            line = line.strip()

            # Check for document environment
            if '\\begin{document}' in line:
                document_node = self.structure_store.append(None, ["Document", "document", i])
                if preamble_node is None:
                    preamble_node = self.structure_store.append(None, ["Preamble", "preamble", 0])
                continue
# silktex.py - Main application class
import os
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, Adw, Gio, GLib

from window import SilkTexWindow
from config import ConfigManager


class SilkTexApp(Adw.Application):
    """Main application class for SilkTex"""
    
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        
        # Set flags for the application
        self.set_flags(Gio.ApplicationFlags.HANDLES_OPEN | 
                      Gio.ApplicationFlags.NON_UNIQUE)
        
        # Load configuration
        self.config = ConfigManager()
        
        # Add actions to the application
        self.create_action('quit', self.on_quit_action, ['<primary>q'])
        self.create_action('new', self.on_new_action, ['<primary>n'])
        self.create_action('open', self.on_open_action, ['<primary>o'])
        self.create_action('save', self.on_save_action, ['<primary>s'])
        self.create_action('save-as', self.on_save_as_action, ['<shift><primary>s'])
        self.create_action('about', self.on_about_action)
        self.create_action('preferences', self.on_preferences_action)
    
    def do_activate(self):
        """Handle application activation (e.g., when launched)"""
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self, config=self.config)
        win.present()
    
    def do_open(self, files, n_files, hint):
        """Handle file open requests"""
        window = self.props.active_window
        if not window:
            window = SilkTexWindow(application=self, config=self.config)
        
        for file in files:
            # Open each file in the window
            window.load_file(file.get_path())
        
        window.present()
    
    def on_quit_action(self, widget, _):
        """Quit the application"""
        windows = self.get_windows()
        for window in windows:
            window.close()
    
    def on_new_action(self, widget, _):
        """Create a new document"""
        win = self.props.active_window
        if win:
            win.new_document()
    
    def on_open_action(self, widget, _):
        """Open a document"""
        win = self.props.active_window
        if win:
            win.open_document()
    
    def on_save_action(self, widget, _):
        """Save the current document"""
        win = self.props.active_window
        if win:
            win.save_document()
    
    def on_save_as_action(self, widget, _):
        """Save the current document with a new name"""
        win = self.props.active_window
        if win:
            win.save_document_as()
    
    def on_about_action(self, widget, _):
        """Show the about dialog"""
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                                application_name='SilkTex',
                                application_icon='org.example.silktex',
                                developer_name='SilkTex Team',
                                version='0.1.0',
                                developers=['SilkTex Developers'],
                                copyright='© 2023 SilkTex Team')
        about.present()
    
    def on_preferences_action(self, widget, _):
        """Show the preferences dialog"""
        win = self.props.active_window
        if win:
            win.show_preferences()
    
    def create_action(self, name, callback, shortcuts=None):
        """Create a simple action and add it to the application"""
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)
            # Check if we're still in preamble (before document begins)
            if document_node is None and not preamble_node and ('\\documentclass' in line or '\\usepackage' in line):
                preamble_node = self.structure_store.append(None, ["Preamble", "preamble", 0])

            # If we're in preamble, add important preamble commands
            if document_node is None and preamble_node and not '\\begin{document}' in line:
                if '\\documentclass' in line:
                    self.structure_store.append(preamble_node, ["Document Class", "command", i])
                elif '\\usepackage' in line:
                    package_match = re.search(r'\\usepackage(?:\[.*?\])?\{(.*?)\}', line)
                    if package_match:
                        package_name = package_match.group(1)
                        self.structure_store.append(preamble_node, [f"Package: {package_name}", "package", i])
                elif '\\title' in line:
                    title_match = re.search(r'\\title\{(.*?)\}', line)
                    if title_match:
                        title_text = title_match.group(1)
                        self.structure_store.append(preamble_node, [f"Title: {title_text}", "title", i])
                elif '\\author' in line:
                    author_match = re.search(r'\\author\{(.*?)\}', line)
                    if author_match:
                        author_text = author_match.group(1)
                        self.structure_store.append(preamble_node, [f"Author: {author_text}", "author", i])

            # Check for section commands (only process these after \begin{document})
            if document_node is not None:
                if line.startswith('\\chapter{'):
                    title_match = re.search(r'\\chapter\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        node = self.structure_store.append(document_node, [f"Chapter: {title}", "chapter", i])
                        section_stack = [document_node, node]
                elif line.startswith('\\section{'):
                    title_match = re.search(r'\\section\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if document_node:
                            node = self.structure_store.append(document_node, [f"Section: {title}", "section", i])
                            section_stack = [document_node, node]
                        else:
                            node = self.structure_store.append(None, [f"Section: {title}", "section", i])
                            section_stack = [None, node]
                elif line.startswith('\\subsection{'):
                    title_match = re.search(r'\\subsection\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if len(section_stack) >= 2:
                            node = self.structure_store.append(section_stack[1], [f"Subsection: {title}", "subsection", i])
                        else:
                            node = self.structure_store.append(document_node, [f"Subsection: {title}", "subsection", i])
                elif line.startswith('\\subsubsection{'):
                    title_match = re.search(r'\\subsubsection\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if len(section_stack) >= 2:
                            node = self.structure_store.append(section_stack[1], [f"Subsubsection: {title}", "subsubsection", i])
                        else:
                            node = self.structure_store.append(document_node, [f"Subsubsection: {title}", "subsubsection", i])
                # Detect figure and table environments
                elif '\\begin{figure}' in line:
                    parent = document_node if document_node else None
                    figure_node = self.structure_store.append(parent, ["Figure", "figure", i])
                    # Search for caption in next few lines
                    for j in range(i+1, min(i+10, len(lines))):
                        if '\\caption{' in lines[j]:
                            caption_match = re.search(r'\\caption\{(.*?)\}', lines[j])
                            if caption_match:
                                self.structure_store.set_value(figure_node, 0, f"Figure: {caption_match.group(1)}")
                            break
                        if '\\end{figure}' in lines[j]:
                            break
                elif '\\begin{table}' in line:


class LaTeXSnippets:
    def __init__(self):
        self.snippets = {
            # Document structure
            "\\begin": "\\begin{$1}\n\t$0\n\\end{$1}",
            "\\section": "\\section{$1}\n$0",
            "\\subsection": "\\subsection{$1}\n$0",
            "\\subsubsection": "\\subsubsection{$1}\n$0",
            "\\chapter": "\\chapter{$1}\n$0",
            "\\paragraph": "\\paragraph{$1} $0",
            "\\newpage": "\\newpage\n$0",
            "\\tableofcontents": "\\tableofcontents\n$0",
            
            # Environments
            "\\document": "\\begin{document}\n$0\n\\end{document}",
            "\\figure": "\\begin{figure}[${1:htbp}]\n\t\\centering\n\t\\includegraphics[width=${2:0.8}\\textwidth]{${3:filename}}\n\t\\caption{${4:caption}}\n\t\\label{fig:${5:label}}\n\\end{figure}",
            "\\table": "\\begin{table}[${1:htbp}]\n\t\\centering\n\t\\begin{tabular}{${2:c c c}}\n\t\t${3:header1} & ${4:header2} & ${5:header3} \\\\\n\t\t\\hline\n\t\t${6:cell1} & ${7:cell2} & ${8:cell3} \\\\\n\t\t${0:cell4} & ${0:cell5} & ${0:cell6} \\\\\n\t\\end{tabular}\n\t\\caption{${9:caption}}\n\t\\label{tab:${10:label}}\n\\end{table}",
            "\\equation": "\\begin{equation}\n\t$0\n\\end{equation}",
            "\\align": "\\begin{align}\n\t$0\n\\end{align}",
            "\\itemize": "\\begin{itemize}\n\t\\item $0\n\\end{itemize}",
            "\\enumerate": "\\begin{enumerate}\n\t\\item $0\n\\end{enumerate}",
            "\\description": "\\begin{description}\n\t\\item[$1] $0\n\\end{description}",
            "\\verbatim": "\\begin{verbatim}\n$0\n\\end{verbatim}",
            "\\center": "\\begin{center}\n\t$0\n\\end{center}",
            "\\quote": "\\begin{quote}\n\t$0\n\\end{quote}",
            "\\abstract": "\\begin{abstract}\n\t$0\n\\end{abstract}",
            
            # Math
            "\\frac": "\\frac{$1}{$2}$0",
            "\\sum": "\\sum_{$1}^{$2} $0",
            "\\int": "\\int_{$1}^{$2} $0",
            "\\sqrt": "\\sqrt{$1}$0",
            "\\matrix": "\\begin{matrix}\n\t$1 & $2 \\\\\n\t$3 & $4\n\\end{matrix}$0",
            "\\lim": "\\lim_{$1 \\to $2} $0",
            
            # Citations and references
            "\\cite": "\\cite{$1}$0",
            "\\ref": "\\ref{$1}$0",
            "\\label": "\\label{$1}$0",
            "\\footnote": "\\footnote{$1}$0",
            "\\bibliographystyle": "\\bibliographystyle{${1:plain}}\n\\bibliography{${2:references}}$0",
            
            # Formatting
            "\\textbf": "\\textbf{$1}$0",
            "\\textit": "\\textit{$1}$0",
            "\\texttt": "\\texttt{$1}$0",
            "\\underline": "\\underline{$1}$0",
            "\\emph": "\\emph{$1}$0",
            "\\textcolor": "\\textcolor{$1}{$2}$0",
            
            # Document preamble
            "\\documentclass": "\\documentclass[${1:11pt}]{${2:article}}\n$0",
            "\\usepackage": "\\usepackage{$1}$0",
            "\\title": "\\title{$1}$0",
            "\\author": "\\author{$1}$0",
            "\\date": "\\date{$1}$0",
            "\\maketitle": "\\maketitle\n$0",
            
            # Templates
            "article": "\\documentclass[11pt]{article}\n\\usepackage{amsmath}\n\\usepackage{graphicx}\n\\usepackage{hyperref}\n\\usepackage[utf8]{inputenc}\n\\usepackage[english]{babel}\n\n\\title{${1:Title}}\n\\author{${2:Author}}\n\\date{\\today}\n\n\\begin{document}\n\n\\maketitle\n\n\\begin{abstract}\n${3:Abstract}\n\\end{abstract}\n\n\\section{Introduction}\n$0\n\n\\section{Conclusion}\n\n\\bibliographystyle{plain}\n\\bibliography{references}\n\n\\end{document}",
            "beamer": "\\documentclass{beamer}\n\\usepackage{amsmath}\n\\usepackage{graphicx}\n\\usepackage{hyperref}\n\\usepackage[utf8]{inputenc}\n\n\\title{${1:Presentation Title}}\n\\author{${2:Author}}\n\\date{\\today}\n\n\\begin{document}\n\n\\frame{\\titlepage}\n\n\\begin{frame}\n\\frametitle{${3:First Frame}}\n$0\n\\end{frame}\n\n\\end{document}",
            "letter": "\\documentclass{letter}\n\\usepackage[utf8]{inputenc}\n\\address{${1:Sender's Address}}\n\\signature{${2:Sender's Name}}\n\n\\begin{document}\n\n\\begin{letter}{${3:Recipient's Address}}\n\n\\opening{${4:Dear Sir or Madam,}}\n\n$0\n\n\\closing{${5:Yours sincerely,}}\n\n\\end{letter}\n\n\\end{document}"
        }
    
    def get_snippet(self, trigger):
        """Get snippet content for a trigger"""
        return self.snippets.get(trigger)
    
    def get_all_snippets(self):
        """Get all available snippets"""
        return self.snippets
    
    def get_matching_snippets(self, prefix):
        """Get snippets that match the given prefix"""
        return {k: v for k, v in self.snippets.items() if k.startswith(prefix)}
    
    
    class LatexCompletionProvider(GObject.GObject, GtkSource.CompletionProvider):
    __gtype_name__ = 'LatexCompletionProvider'
    
    def __init__(self):
        super().__init__()
        self.snippets = LaTeXSnippets()
    
    def do_get_name(self):
        return "LaTeX"
    
    def do_populate(self, context):
        iter = context.get_iter()
        buffer = iter.get_buffer()
        
        # Get the text from the beginning of the line to the cursor position
        line_start = iter.copy()
        line_start.set_line_offset(0)
        text = buffer.get_text(line_start, iter, False)
        
        # Find the word being typed (starting with \)
        match = re.search(r'\\([a-zA-Z]*)$', text)
        if not match:
            context.add_proposals(self, [], True)
            return
        
        prefix = '\\' + match.group(1)
        matching_snippets = self.snippets.get_matching_snippets(prefix)
        
        proposals = []
        for trigger, content in matching_snippets.items():
            # Create a display string that shows the snippet structure
            display = trigger + " → " + content.split("\n")[0].replace("$1", "...").replace("$0", "")
            
            # Create a proposal
            proposal = GtkSource.CompletionItem.new()
            proposal.set_label(display)
            proposal.set_text(trigger)
            proposals.append(proposal)
        
        context.add_proposals(self, proposals, True)
    
    
    class LatexSourceView(GtkSource.View):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        
        # Set up the source view
        self.set_vexpand(True)
        self.set_hexpand(True)
        self.set_monospace(True)
        self.set_auto_indent(True)
        self.set_indent_on_tab(True)
        self.set_tab_width(2)
        self.set_insert_spaces_instead_of_tabs(True)
        self.set_smart_backspace(True)
        self.set_background_pattern(GtkSource.BackgroundPatternType.GRID)
        
        # Set up the buffer with syntax highlighting
        self.buffer = GtkSource.Buffer()
        self.set_buffer(self.buffer)
        
        # Set up language for syntax highlighting
        language_manager = GtkSource.LanguageManager.get_default()
        language = language_manager.get_language("latex")
        if language:
            self.buffer.set_language(language)
            self.buffer.set_highlight_syntax(True)
        
        # Set up style scheme
        scheme_manager = GtkSource.StyleSchemeManager.get_default()
        scheme = scheme_manager.get_scheme("kate")
        if scheme:
            self.buffer.set_style_scheme(scheme)
        
        # Line numbers and other display features
        self.set_show_line_numbers(True)
        self.set_highlight_current_line(True)
        
        # Set up font
        font_desc = Pango.FontDescription("Monospace 10")
        self.override_font(font_desc)
        
        # Set up completion
        self.completion = self.get_completion()
        self.completion_provider = LatexCompletionProvider()
        self.completion.add_provider(self.completion_provider)
        
        # Snippet support
        self.snippets = LaTeXSnippets()
        
        # Track current file
        self.current_file = None
        
        # Connect signals
        self.buffer.connect("changed", self.on_buffer_changed)
        self.buffer.connect("modified-changed", self.on_modified_changed)
        self.connect("key-pressed", self.on_key_pressed)
        
        # Create custom content-changed signal
        GLib.signal_new("content-changed", LatexSourceView, 
                        GLib.SignalFlags.RUN_LAST, GLib.TYPE_NONE, [GLib.TYPE_STRING])
        
        # Create custom file-loaded signal
        GLib.signal_new("file-loaded", LatexSourceView, 
                        GLib.SignalFlags.RUN_LAST, GLib.TYPE_NONE, [GLib.TYPE_STRING])
                        
        # Create custom modification-state-changed signal
        GLib.signal_new("modification-state-changed", LatexSourceView, 
                        GLib.SignalFlags.RUN_LAST, GLib.TYPE_NONE, [GLib.TYPE_BOOLEAN])
        
        # Example LaTeX template
        self.default_text = """\\documentclass{article}
    \\usepackage{amsmath}
    \\usepackage{graphicx}
    \\usepackage{hyperref}
    \\usepackage{xcolor}
    
    \\title{SilkTex Example Document}
    \\author{Your Name}
    \\date{\\today}
    
    \\begin{document}
    
    \\maketitle
    
    \\section{Introduction}
    This is an example LaTeX document created with SilkTex. SilkTex is a modern GTK4-based LaTeX editor with live preview.
    
    \\subsection{Features}
    SilkTex includes:
    \\begin{itemize}
      \\item Live preview of your LaTeX document
      \\item Syntax highlighting
      \\item Dark mode support
      \\item Document structure navigation
      \\item LaTeX snippets and autocompletion
    \\end{itemize}
    
    \\section{Equations}
    You can write beautiful equations like this:
    \\begin{equation}
      E = mc^2
    \\end{equation}
    
    Or more complex ones:
    \\begin{align}
      \\nabla \\times \\vec{\\mathbf{B}} -\\, \\frac1c\\, \\frac{\\partial\\vec{\\mathbf{E}}}{\\partial t} & = \\frac{4\\pi}{c}\\vec{\\mathbf{j}} \\\\
      \\nabla \\cdot \\vec{\\mathbf{E}} & = 4 \\pi \\rho \\\\
      \\nabla \\times \\vec{\\mathbf{E}}\\, +\\, \\frac1c\\, \\frac{\\partial\\vec{\\mathbf{B}}}{\\partial t} & = \\vec{\\mathbf{0}} \\\\
      \\nabla \\cdot \\vec{\\mathbf{B}} & = 0
    \\end{align}
    
    \\section{Conclusion}
    This is just a simple example document. Type '\\begin' and press Tab to try the snippet system!
    
    \\end{document}
    """
        
        # Set the default text
        self.buffer.set_text(self.default_text)
        self.buffer.set_modified(False)
    
    def on_buffer_changed(self, buffer):
        """Handle buffer content changes"""
        start_iter = buffer.get_start_iter()
        end_iter = buffer.get_end_iter()
        text = buffer.get_text(start_iter, end_iter, False)
        
        # Emit a custom signal when content changes
        self.emit("content-changed", text)
    
    def on_modified_changed(self, buffer):
        """Track document modification state"""
        modified = buffer.get_modified()
        self.emit("modification-state-changed", modified)
    
    def on_key_pressed(self, view, keyval, keycode, state):
        """Handle key presses for snippet expansion and auto-completion"""
        # Tab key for snippet expansion
        if keyval == Gdk.KEY_Tab and not (state & Gdk.ModifierType.CONTROL_MASK):
            buffer = self.get_buffer()
            cursor = buffer.get_iter_at_mark(buffer.get_insert())
            
            # Get the word before the cursor
            line_start = cursor.copy()
            line_start.set_line_offset(0)
            line_text = buffer.get_text(line_start, cursor, False)
            
            # Check if we're typing a LaTeX command (starting with \)
            match = re.search(r'\\([a-zA-Z]+)$', line_text)
            if match:
                trigger = '\\' + match.group(1)
                snippet_content = self.snippets.get_snippet(trigger)
                
                if snippet_content:
                    # Delete the trigger text
                    start_pos = cursor.copy()
                    start_pos.backward_chars(len(trigger))
                    buffer.delete(start_pos, cursor)
                    
                    # Insert the snippet
                    buffer.insert_at_cursor(snippet_content)
                    
                    # Find placeholder positions
                    text = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), False)
                    # TODO: Implement full placeholder navigation
                    
                    return True  # Stop event propagation
            
        # Auto-pairs for brackets and quotes
        if keyval == Gdk.KEY_parenleft:  # (
            buffer = self.get_buffer()
            buffer.insert_at_cursor("()")
            cursor = buffer.get_iter_at_mark(buffer.get_insert())
            cursor.backward_char()
            buffer.place_cursor(cursor)
            return True
            
        elif keyval == Gdk.KEY_braceleft:  # {
            buffer = self.get_buffer()
            buffer.insert_at_cursor("{}")
            cursor = buffer.get_iter_at_mark(buffer.get_insert())
            cursor.backward_char()
            buffer.place_cursor(cursor)
            return True
            
        elif keyval == Gdk.KEY_bracketleft:  # [
            buffer = self.get_buffer()
            buffer.insert_at_cursor("[]")
            cursor = buffer.get_iter_at_mark(buffer.get_insert())
            cursor.backward_char()
            buffer.place_cursor(cursor)
            return True
            
        elif keyval == Gdk.KEY_quotedbl:  # "
            buffer = self.get_buffer()
            buffer.insert_at_cursor("\"\"")
            cursor = buffer.get_iter_at_mark(buffer.get_insert())
            cursor.backward_char()
            buffer.place_cursor(cursor)
            return True
            
        elif keyval == Gdk.KEY_apostrophe:  # '
            buffer = self.get_buffer()
            buffer.insert_at_cursor("''")
            cursor = buffer.get_iter_at_mark(buffer.get_insert())
            cursor.backward_char()
            buffer.place_cursor(cursor)
            return True
        
        return False  # Continue event propagation
    
    def get_text(self):
        """Get the current text in the buffer"""
        start_iter = self.buffer.get_start_iter()
        end_iter = self.buffer.get_end_iter()
        return self.buffer.get_text(start_iter, end_iter, False)
    
    def is_modified(self):
        """Check if the document has been modified"""
        return self.buffer.get_modified()
    
    def load_file(self, file_path):
        """Load a file into the editor"""
        try:
            with open(file_path, 'r') as f:
                content = f.read()
                self.buffer.set_text(content)
                self.buffer.set_modified(False)
                self.current_file = file_path
                
                # Emit signal that file was loaded
                self.emit("file-loaded", file_path)
                
                return True
        except Exception as e:
            print(f"Error loading file: {e}")
            return False
    
    def save_file(self, file_path=None):
        """Save the current content to a file"""
        if file_path is None:
            file_path = self.current_file
            
        if file_path is None:
            # No file path specified and no current file
            return False
            
        try:
            content = self.get_text()
            with open(file_path, 'w') as f:
                f.write(content)
                
            self.buffer.set_modified(False)
            self.current_file = file_path
            return True
        except Exception as e:
            print(f"Error saving file: {e}")
            return False

    def get_snippet(self, trigger):
        """Get snippet content for a trigger"""
        return self.snippets.get(trigger)

    def get_all_snippets(self):
        """Get all available snippets"""
        return self.snippets

    def get_matching_snippets(self, prefix):
        """Get snippets that match the given prefix"""
        return {k: v for k, v in self.snippets.items() if k.startswith(prefix)}


    class LatexCompletionProvider(GObject.GObject, GtkSource.CompletionProvider):
    __gtype_name__ = 'LatexCompletionProvider'

    def __init__(self):
        super().__init__()
        self.snippets = LaTeXSnippets()

    def do_get_name(self):
        return "LaTeX"

    def do_populate(self, context):
        iter = context.get_iter()
        buffer = iter.get_buffer()

        # Get the text from the beginning of the line to the cursor position
        line_start = iter.copy()
        line_start.set_line_offset(0)
        text = buffer.get_text(line_start, iter, False)

        # Find the word being typed (starting with \)
        match = re.search(r'\\([a-zA-Z]*)$', text)
        if not match:
            context.add_proposals(self, [], True)
            return

        prefix = '\\' + match.group(1)
        matching_snippets = self.snippets.get_matching_snippets(prefix)

        proposals = []
        for trigger, content in matching_snippets.items():
            # Create a display string that shows the snippet structure
            display = trigger + " → " + content.split("\n")[0].replace("$1", "...").replace("$0", "")

            # Create a proposal
            proposal = GtkSource.CompletionItem.new()
            proposal.set_label(display)
            proposal.set_text(trigger)
            proposals.append(proposal)

        context.add_proposals(self, proposals, True)
        
        
        class DocumentStructure(Gtk.Box):
            def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.set_orientation(Gtk.Orientation.VERTICAL)
        
        # Create a title
        title = Gtk.Label()
        title.set_markup("<b>Document Structure</b>")
        title.set_halign(Gtk.Align.START)
        title.set_margin_bottom(10)
        title.set_margin_top(10)
        title.set_margin_start(10)
        self.append(title)
        
        # Create a scrolled window
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        
        # Create a tree view for document structure
        self.structure_store = Gtk.TreeStore(str, str, int)  # Text, Type, Line number
        self.structure_view = Gtk.TreeView(model=self.structure_store)
        self.structure_view.set_headers_visible(False)
        
        renderer = Gtk.CellRendererText()
        column = Gtk.TreeViewColumn("Section", renderer, text=0)
        self.structure_view.append_column(column)
        
        # Connect to selection change
        select = self.structure_view.get_selection()
        select.connect("changed", self.on_selection_changed)
        
        scrolled.set_child(self.structure_view)
        self.append(scrolled)
        
        # Add a button box at the bottom
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_margin_top(10)
        button_box.set_margin_bottom(10)
        button_box.set_margin_start(10)
        button_box.set_margin_end(10)
        button_box.set_spacing(5)
        
        refresh_button = Gtk.Button(label="Refresh")
        refresh_button.set_hexpand(True)
        refresh_button.connect("clicked", self.on_refresh_clicked)
        button_box.append(refresh_button)
        
        self.append(button_box)
        
        # Store reference to source view
        self.source_view = None
            
            def set_source_view(self, source_view):
        """Set the source view to navigate to when an item is selected"""
        self.source_view = source_view
            
            def on_selection_changed(self, selection):
        """Navigate to the selected section in the document"""
        if not self.source_view:
            return
            
        model, treeiter = selection.get_selected()
        if treeiter is not None:
            line_num = model[treeiter][2]
            if line_num >= 0:
                # Navigate to the line in the source view
                buffer = self.source_view.get_buffer()
                line_iter = buffer.get_iter_at_line(line_num)
                buffer.place_cursor(line_iter)
                self.source_view.scroll_to_iter(line_iter, 0.25, False, 0.0, 0.0)
            
            def on_refresh_clicked(self, button):
        """Refresh the document structure"""
        if self.source_view:
            buffer = self.source_view.get_buffer()
            start_iter = buffer.get_start_iter()
            end_iter = buffer.get_end_iter()
            text = buffer.get_text(start_iter, end_iter, False)
            self.update_structure(text)
            
            def update_structure(self, tex_content):
        """Update the document structure based on LaTeX content"""
        self.structure_store.clear()
        
        # Simple regex-based parsing of LaTeX structure
        lines = tex_content.split('\n')
        section_stack = [None]  # Root
        document_node = None
        preamble_node = None
        
        for i, line in enumerate(lines):
            line = line.strip()
            
            # Check for document environment
            if '\\begin{document}' in line:
                document_node = self.structure_store.append(None, ["Document", "document", i])
                if preamble_node is None:
                    preamble_node = self.structure_store.append(None, ["Preamble", "preamble", 0])
                continue
                
            # Check if we're still in preamble (before document begins)
            if document_node is None and not preamble_node and ('\\documentclass' in line or '\\usepackage' in line):
                preamble_node = self.structure_store.append(None, ["Preamble", "preamble", 0])
            
            # If we're in preamble, add important preamble commands
            if document_node is None and preamble_node and not '\\begin{document}' in line:
                if '\\documentclass' in line:
                    self.structure_store.append(preamble_node, ["Document Class", "command", i])
                elif '\\usepackage' in line:
                    package_match = re.search(r'\\usepackage(?:\[.*?\])?\{(.*?)\}', line)
                    if package_match:
                        package_name = package_match.group(1)
                        self.structure_store.append(preamble_node, [f"Package: {package_name}", "package", i])
                elif '\\title' in line:
                    title_match = re.search(r'\\title\{(.*?)\}', line)
                    if title_match:
                        title_text = title_match.group(1)
                        self.structure_store.append(preamble_node, [f"Title: {title_text}", "title", i])
                elif '\\author' in line:
                    author_match = re.search(r'\\author\{(.*?)\}', line)
                    if author_match:
                        author_text = author_match.group(1)
                        self.structure_store.append(preamble_node, [f"Author: {author_text}", "author", i])
            
            # Check for section commands (only process these after \begin{document})
            if document_node is not None:
                if line.startswith('\\chapter{'):
                    title_match = re.search(r'\\chapter\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        node = self.structure_store.append(document_node, [f"Chapter: {title}", "chapter", i])
                        section_stack = [document_node, node]
                elif line.startswith('\\section{'):
                    title_match = re.search(r'\\section\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if document_node:
                            node = self.structure_store.append(document_node, [f"Section: {title}", "section", i])
                            section_stack = [document_node, node]
                        else:
                            node = self.structure_store.append(None, [f"Section: {title}", "section", i])
                            section_stack = [None, node]
                elif line.startswith('\\subsection{'):
                    title_match = re.search(r'\\subsection\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if len(section_stack) >= 2:
                            node = self.structure_store.append(section_stack[1], [f"Subsection: {title}", "subsection", i])
                        else:
                            node = self.structure_store.append(document_node, [f"Subsection: {title}", "subsection", i])
                elif line.startswith('\\subsubsection{'):
                    title_match = re.search(r'\\subsubsection\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if len(section_stack) >= 2:
                            node = self.structure_store.append(section_stack[1], [f"Subsubsection: {title}", "subsubsection", i])
                        else:
                            node = self.structure_store.append(document_node, [f"Subsubsection: {title}", "subsubsection", i])
                # Detect figure and table environments
                elif '\\begin{figure}' in line:
                    parent = document_node if document_node else None
                    figure_node = self.structure_store.append(parent, ["Figure", "figure", i])
                    # Search for caption in next few lines
                    for j in range(i+1, min(i+10, len(lines))):
                        if '\\caption{' in lines[j]:
                            caption_match = re.search(r'\\caption\{(.*?)\}', lines[j])
                            if caption_match:
                                self.structure_store.set_value(figure_node, 0, f"Figure: {caption_match.group(1)}")
                            break
                        if '\\end{figure}' in lines[j]:
                            break
                elif '\\begin{table}' in line:
                    parent = document_node if document_node else None
                    table_node = self.structure_store.append(parent, ["Table", "table", i])
                    # Search for caption in next few lines
                    for j in range(i+1, min(i+15, len(lines))):
                        if '\\caption{' in lines[j]:
                            caption_match = re.search(r'\\caption\{(.*?)\}', lines[j])
                            if caption_match:
                                self.structure_store.set_value(table_node, 0, f"Table: {caption_match.group(1)}")
                            break
                        if '\\end{table}' in lines[j]:
                            break
                
                # Detect bibliography
                elif '\\bibliography{' in line or '\\begin{thebibliography}' in line:
                    parent = document_node if document_node else None
                    self.structure_store.append(parent, ["Bibliography", "bibliography", i])
                
                # Detect appendix
                elif line == '\\appendix':
                    parent = document_node if document_node else None
                    self.structure_store.append(parent, ["Appendices", "appendix", i])
        
        # Expand all nodes
        self.structure_view.expand_all()


        class DocumentStructure(Gtk.Box):
            def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.set_orientation(Gtk.Orientation.VERTICAL)

        # Create a title
        title = Gtk.Label()
        title.set_markup("<b>Document Structure</b>")
        title.set_halign(Gtk.Align.START)
        title.set_margin_bottom(10)
        title.set_margin_top(10)
        title.set_margin_start(10)
        self.append(title)

        # Create a scrolled window
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)

        # Create a tree view for document structure
        self.structure_store = Gtk.TreeStore(str, str, int)  # Text, Type, Line number
        self.structure_view = Gtk.TreeView(model=self.structure_store)
        self.structure_view.set_headers_visible(False)

        renderer = Gtk.CellRendererText()
        column = Gtk.TreeViewColumn("Section", renderer, text=0)
        self.structure_view.append_column(column)

        # Connect to selection change
        select = self.structure_view.get_selection()
        select.connect("changed", self.on_selection_changed)

        scrolled.set_child(self.structure_view)
        self.append(scrolled)

        # Add a button box at the bottom
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_margin_top(10)
        button_box.set_margin_bottom(10)
        button_box.set_margin_start(10)
        button_box.set_margin_end(10)
        button_box.set_spacing(5)

        refresh_button = Gtk.Button(label="Refresh")
        refresh_button.set_hexpand(True)
        refresh_button.connect("clicked", self.on_refresh_clicked)
        button_box.append(refresh_button)

        self.append(button_box)

        # Store reference to source view
        self.source_view = None

            def set_source_view(self, source_view):
        """Set the source view to navigate to when an item is selected"""
        self.source_view = source_view

            def on_selection_changed(self, selection):
        """Navigate to the selected section in the document"""
        if not self.source_view:
            return
class LaTeXPreview(Gtk.Box):
    """Preview component for rendering LaTeX content"""
    
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.set_orientation(Gtk.Orientation.VERTICAL)
        
        # Create a WebKit WebView for PDF display
        self.webview = WebKit.WebView()
        self.webview.set_vexpand(True)
        
        # Create a scroll window for the web view
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        scrolled.set_child(self.webview)
        
        # Add toolbar with refresh and zoom controls
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        toolbar.set_margin_top(5)
        toolbar.set_margin_bottom(5)
        toolbar.set_margin_start(5)
        toolbar.set_margin_end(5)
        toolbar.set_spacing(5)
        
        # Refresh button
        self.refresh_button = Gtk.Button()
        self.refresh_button.set_icon_name("view-refresh-symbolic")
        self.refresh_button.set_tooltip_text("Refresh Preview")
        self.refresh_button.connect("clicked", self.on_refresh_clicked)
        toolbar.append(self.refresh_button)
        
        # Add spacing
        toolbar.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))
        
        # Zoom out button
        zoom_out_button = Gtk.Button()
        zoom_out_button.set_icon_name("zoom-out-symbolic")
        zoom_out_button.set_tooltip_text("Zoom Out")
        zoom_out_button.connect("clicked", self.on_zoom_out_clicked)
        toolbar.append(zoom_out_button)
        
        # Zoom label
        self.zoom_level = 1.0
        self.zoom_label = Gtk.Label(label="100%")
        self.zoom_label.set_margin_start(5)
        self.zoom_label.set_margin_end(5)
        toolbar.append(self.zoom_label)
        
        # Zoom in button
        zoom_in_button = Gtk.Button()
        zoom_in_button.set_icon_name("zoom-in-symbolic")
        zoom_in_button.set_tooltip_text("Zoom In")
        zoom_in_button.connect("clicked", self.on_zoom_in_clicked)
        toolbar.append(zoom_in_button)
        
        # Add spacing
        toolbar.append(Gtk.Separator(orientation=Gtk.Orientation.VERTICAL))
        
        # Auto-refresh toggle
        self.auto_refresh = Gtk.CheckButton(label="Auto-refresh")
        self.auto_refresh.set_active(True)
        self.auto_refresh.set_tooltip_text("Automatically refresh preview when content changes")
        toolbar.append(self.auto_refresh)
        
        # Status label (right-aligned)
        self.status_label = Gtk.Label(label="Ready")
        self.status_label.set_hexpand(True)
        self.status_label.set_halign(Gtk.Align.END)
        toolbar.append(self.status_label)
        
        # Add components to the main box
        self.append(toolbar)
        self.append(scrolled)
        
        # Initialize variables
        self.current_file = None
        self.temp_dir = None
        self.compilation_in_progress = False
        self.latex_content = None
        self.compile_thread = None
        self.last_compile_time = 0
        self.compile_interval = 2.0  # Minimum time between compiles in seconds
        
        # Create a temporary directory for compilation
        self.create_temp_dir()
        
        # Load default content
        self.load_default_content()
    
    def create_temp_dir(self):
        """Create a temporary directory for LaTeX compilation"""
        try:
            self.temp_dir = tempfile.mkdtemp(prefix="silktex_")
            print(f"Created temporary directory: {self.temp_dir}")
        except Exception as e:
            print(f"Error creating temporary directory: {e}")
            self.temp_dir = None
    
    def cleanup_temp_dir(self):
        """Clean up the temporary directory"""
        if self.temp_dir and os.path.exists(self.temp_dir):
            try:
                # Remove all files in the temporary directory
                for file in os.listdir(self.temp_dir):
                    file_path = os.path.join(self.temp_dir, file)
                    try:
                        if os.path.isfile(file_path):
                            os.unlink(file_path)
                    except Exception as e:
                        print(f"Error removing file {file_path}: {e}")
                
                # Remove the directory itself
                os.rmdir(self.temp_dir)
                print(f"Removed temporary directory: {self.temp_dir}")
                self.temp_dir = None
            except Exception as e:
                print(f"Error cleaning up temporary directory: {e}")
    
    def load_default_content(self):
        """Load default content when no file is open"""
        default_html = """
        <html>
        <head>
            <style>
                body {
                    font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif;
                    display: flex;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    margin: 0;
                    color: #333;
                    background-color: #f5f5f5;
                    text-align: center;
                }
                .container {
                    max-width: 80%;
                }
                h1 {
                    color: #2a76dd;
                }
                p {
                    margin: 10px 0;
                    line-height: 1.5;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>SilkTex Preview</h1>
                <p>Create or open a LaTeX document to preview it here.</p>
                <p>The preview will update automatically as you type.</p>
            </div>
        </body>
        </html>
        """
        self.webview.load_html(default_html, "file:///")
        self.status_label.set_text("Ready")
    
    def update_preview(self, latex_content, file_path=None):
        """Update the preview with new LaTeX content"""
        self.latex_content = latex_content
        self.current_file = file_path
        
        # Don't compile if compilation is already in progress
        if self.compilation_in_progress:
            return
        
        # Check if enough time has passed since last compile
        current_time = time.time()
        if current_time - self.last_compile_time < self.compile_interval:
            # Schedule compilation for later if auto-refresh is enabled
            if self.auto_refresh.get_active():
                GLib.timeout_add(
                    int((self.last_compile_time + self.compile_interval - current_time) * 1000),
                    self.compile_latex
                )
            return
        
        # Compile if auto-refresh is enabled
        if self.auto_refresh.get_active():
            self.compile_latex()
    
    def compile_latex(self):
        """Compile LaTeX to PDF in a separate thread"""
        if self.compilation_in_progress:
            return False
        
        if not self.latex_content:
            return False
        
        if not self.temp_dir:
            self.create_temp_dir()
            if not self.temp_dir:
                self.status_label.set_text("Error: Could not create temporary directory")
                return False
        
        self.compilation_in_progress = True
        self.status_label.set_text("Compiling...")
        self.refresh_button.set_sensitive(False)
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(target=self._compile_thread_func)
        self.compile_thread.daemon = True
        self.compile_thread.start()
        
        # Save time of compilation start
        self.last_compile_time = time.time()
        
        return False  # Don't repeat this timer
    
    def _compile_thread_func(self):
        """Thread function for LaTeX compilation"""
        result = False
        error_message = None
        
        try:
            # Write the LaTeX content to a temporary file
            tex_file = os.path.join(self.temp_dir, "preview.tex")
            with open(tex_file, 'w') as f:
                f.write(self.latex_content)
            
            # Run pdflatex
            proc = subprocess.Popen(
                ["pdflatex", "-interaction=nonstopmode", "-halt-on-error", "preview.tex"],
                cwd=self.temp_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            stdout, stderr = proc.communicate()
            
            # Check if compilation was successful
            if proc.returncode == 0:
                result = True
            else:
                # Parse error message from output
                error_match = re.search(r'!(.*?)\nl\.(\d+)', stdout, re.DOTALL)
                if error_match:
                    error_type = error_match.group(1).strip()
                    line_num = error_match.group(2)
                    error_message = f"Error on line {line_num}: {error_type}"
                else:
                    error_message = "Compilation failed with unknown error"
        
        except Exception as e:
            error_message = f"Error: {str(e)}"
        
        # Update UI from the main thread
        GLib.idle_add(self._compilation_finished, result, error_message)
    
    def _compilation_finished(self, success, error_message=None):
        """Called when compilation finishes (from main thread)"""
        self.compilation_in_progress = False
        self.refresh_button.set_sensitive(True)
        
        if success:
            # Load the PDF file
            pdf_path = os.path.join(self.temp_dir, "preview.pdf")
            if os.path.exists(pdf_path):
                self.webview.load_uri(f"file://{pdf_path}")
                self.status_label.set_text("Preview updated")
            else:
                self.status_label.set_text("PDF file not found")
        else:
            # Show error message
            error_html = f"""
            <html>
            <head>
                <style>
                    body {{
                        font-family: system-ui, sans-serif;
                        padding: 20px;
                        color: #333;
                        background-color: #f8f8f8;
                    }}
                    .error-box {{
                        border: 1px solid #e74c3c;
                        background-color: #fdedec;
                        padding: 15px;
                        border-radius: 5px;
                        margin-bottom: 20px;
                    }}
                    h2 {{
                        color: #c0392b;
                        margin-top: 0;
                    }}
                    pre {{
                        background-color: #f1f1f1;
                        padding: 10px;
                        border-radius: 3px;
                        overflow-x: auto;
                        font-family: monospace;
                    }}
                </style>
            </head>
            <body>
                <div class="error-box">
                    <h2>LaTeX Compilation Error</h2>
                    <pre>{error_message or "Unknown error occurred during compilation."}</pre>
                </div>
                <p>Please fix the errors in your LaTeX document and try again.</p>
            </body>
            </html>
            """
            self.webview.load_html(error_html, "file:///")
            self.status_label.set_text("Compilation failed")
        
        return False  # Don't repeat this idle callback
    
    def on_refresh_clicked(self, button):
        """Handle refresh button click"""
        if not self.compilation_in_progress and self.latex_content:
            self.compile_latex()
    
    def on_zoom_in_clicked(self, button):
        """Handle zoom in button click"""
        if self.zoom_level < 2.0:
            self.zoom_level += 0.1
            self.update_zoom()
    
    def on_zoom_out_clicked(self, button):
        """Handle zoom out button click"""
        if self.zoom_level > 0.5:
            self.zoom_level -= 0.1
            self.update_zoom()
    
    def update_zoom(self):
        """Update zoom level in the webview"""
        self.webview.set_zoom_level(self.zoom_level)
        self.zoom_label.set_text(f"{int(self.zoom_level * 100)}%")
        model, treeiter = selection.get_selected()
        if treeiter is not None:
            line_num = model[treeiter][2]
            if line_num >= 0:
                # Navigate to the line in the source view
                buffer = self.source_view.get_buffer()
                line_iter = buffer.get_iter_at_line(line_num)
                buffer.place_cursor(line_iter)
                self.source_view.scroll_to_iter(line_iter, 0.25, False, 0.0, 0.0)

            def on_refresh_clicked(self, button):
        """Refresh the document structure"""
        if self.source_view:
            buffer = self.source_view.get_buffer()
            start_iter = buffer.get_start_iter()
            end_iter = buffer.get_end_iter()
            text = buffer.get_text(start_iter, end_iter, False)
            self.update_structure(text)

            def update_structure(self, tex_content):
        """Update the document structure based on LaTeX content"""
        self.structure_store.clear()

        # Simple regex-based parsing of LaTeX structure
        lines = tex_content.split('\n')
        section_stack = [None]  # Root
        document_node = None
        preamble_node = None

        for i, line in enumerate(lines):
            line = line.strip()

            # Check for document environment
            if '\\begin{document}' in line:
                document_node = self.structure_store.append(None, ["Document", "document", i])
                if preamble_node is None:
                    preamble_node = self.structure_store.append(None, ["Preamble", "preamble", 0])
                continue

            # Check if we're still in preamble (before document begins)
            if document_node is None and not preamble_node and ('\\documentclass' in line or '\\usepackage' in line):
                preamble_node = self.structure_store.append(None, ["Preamble", "preamble", 0])

            # If we're in preamble, add important preamble commands
            if document_node is None and preamble_node and not '\\begin{document}' in line:
                if '\\documentclass' in line:
                    self.structure_store.append(preamble_node, ["Document Class", "command", i])
                elif '\\usepackage' in line:
                    package_match = re.search(r'\\usepackage(?:\[.*?\])?\{(.*?)\}', line)
                    if package_match:
                        package_name = package_match.group(1)
                        self.structure_store.append(preamble_node, [f"Package: {package_name}", "package", i])
                elif '\\title' in line:
                    title_match = re.search(r'\\title\{(.*?)\}', line)
                    if title_match:
                        title_text = title_match.group(1)
                        self.structure_store.append(preamble_node, [f"Title: {title_text}", "title", i])
                elif '\\author' in line:
                    author_match = re.search(r'\\author\{(.*?)\}', line)
                    if author_match:
                        author_text = author_match.group(1)
                        self.structure_store.append(preamble_node, [f"Author: {author_text}", "author", i])

            # Check for section commands (only process these after \begin{document})
            if document_node is not None:
                if line.startswith('\\chapter{'):
                    title_match = re.search(r'\\chapter\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        node = self.structure_store.append(document_node, [f"Chapter: {title}", "chapter", i])
                        section_stack = [document_node, node]
                elif line.startswith('\\section{'):
                    title_match = re.search(r'\\section\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if document_node:
                            node = self.structure_store.append(document_node, [f"Section: {title}", "section", i])
                            section_stack = [document_node, node]
                        else:
                            node = self.structure_store.append(None, [f"Section: {title}", "section", i])
                            section_stack = [None, node]
                elif line.startswith('\\subsection{'):
                    title_match = re.search(r'\\subsection\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if len(section_stack) >= 2:
                            node = self.structure_store.append(section_stack[1], [f"Subsection: {title}", "subsection", i])
                        else:
                            node = self.structure_store.append(document_node, [f"Subsection: {title}", "subsection", i])
                elif line.startswith('\\subsubsection{'):
                    title_match = re.search(r'\\subsubsection\{(.*?)\}', line)
                    if title_match:
                        title = title_match.group(1)
                        if len(section_stack) >= 2:
                            node = self.structure_store.append(section_stack[1], [f"Subsubsection: {title}", "subsubsection", i])
                        else:
                            node = self.structure_store.append(document_node, [f"Subsubsection: {title}", "subsubsection", i])
                # Detect figure and table environments
                elif '\\begin{figure}' in line:
                    parent = document_node if document_node else None
                    figure_node = self.structure_store.append(parent, ["Figure", "figure", i])
                    # Search for caption in next few lines
                    for j in range(i+1, min(i+10, len(lines))):
                        if '\\caption{' in lines[j]:
                            caption_match = re.search(r'\\caption\{(.*?)\}', lines[j])
                            if caption_match:
                                self.structure_store.set_value(figure_node, 0, f"Figure: {caption_match.group(1)}")
                            break
                        if '\\end{figure}' in lines[j]:
                            break
                elif '\\begin{table}' in line:


class SilkTexApplication(Adw.Application):
    def __init__(self):
        super().__init__(application_id='org.example.silktex',
                         flags=Gio.ApplicationFlags.FLAGS_NONE)
        self.create_action('quit', self.quit, ['<primary>q'])
        self.create_action('about', self.on_about_action)
        self.create_action('preferences', self.on_preferences_action)

    def do_activate(self):
        win = self.props.active_window
        if not win:
            win = SilkTexWindow(application=self)
        win.present()

    def on_about_action(self, widget, _):
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                                application_name='SilkTex',
                                application_icon='org.example.silktex',
                                developer_name='Developer',
                                version='0.1.0',
                                developers=['Your Name'],
                                copyright='© 2023 Your Name')
        about.present()

    def on_preferences_action(self, widget, _):
        self.props.active_window.main_stack.set_visible_child_name('settings')

    def create_action(self, name, callback, shortcuts=None):
        action = Gio.SimpleAction.new(name, None)
        action.connect("activate", callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f"app.{name}", shortcuts)


# Add a main function that can be called directly
def main(version=None):
    """Main entry point for the application"""
    app = SilkTexApplication()
    return app.run(sys.argv)

# Allow running the module directly
if __name__ == "__main__":
    sys.exit(main())
