# document_structure.py - Structure view for LaTeX documents
import re
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')

from gi.repository import Gtk, Adw, GObject, Gio, GLib


class DocumentStructure(Gtk.Box):
    """Document structure tree view for navigation"""
    
    __gsignals__ = {
        'item-activated': (GObject.SignalFlags.RUN_FIRST, None, (str, int)),
    }
    
    def __init__(self):
        """Initialize document structure component"""
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        # Create UI elements
        self.create_ui()
        
        # Document sections store
        self.structure_store = Gio.ListStore.new(StructureItem)
        
        # Create tree view
        self.structure_view = Gtk.ListView.new(
            Gtk.NoSelection.new(self.structure_store),
            self.create_factory()
        )
        
        self.scrolled_window.set_child(self.structure_view)
        
        # Connect signals
        self.structure_view.connect('activate', self.on_item_activated)
    
    def create_ui(self):
        """Create the UI elements"""
        # Title
        title_label = Gtk.Label(label="Document Structure")
        title_label.add_css_class("heading")
        title_label.set_halign(Gtk.Align.START)
        title_label.set_margin_top(6)
        title_label.set_margin_bottom(6)
        title_label.set_margin_start(12)
        title_label.set_margin_end(12)
        self.append(title_label)
        
        # Tree View in a scrolled window
        self.scrolled_window = Gtk.ScrolledWindow()
        self.scrolled_window.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        self.scrolled_window.set_vexpand(True)
        self.append(self.scrolled_window)
    
    def create_factory(self):
        """Create the factory for list items"""
        factory = Gtk.SignalListItemFactory()
        factory.connect('setup', self._on_factory_setup)
        factory.connect('bind', self._on_factory_bind)
        return factory
    
    def _on_factory_setup(self, factory, list_item):
        """Set up the list item"""
        row = Adw.ActionRow()
        list_item.set_child(row)
    
    def _on_factory_bind(self, factory, list_item):
        """Bind data to the list item"""
        row = list_item.get_child()
        item = list_item.get_item()
        
        # Set title with appropriate indentation
        title = item.title
        if item.level > 1:
            # Add indentation based on level
            indent = "  " * (item.level - 1)
            title = indent + title
        
        row.set_title(title)
        
        # Style based on level
        if item.level == 1:
            row.add_css_class("heading")
        
        # Store line number for navigation
        row.set_data('line', item.line)
    
    def on_item_activated(self, list_view, position):
        """Handle activation of a structure item"""
        item = self.structure_store.get_item(position)
        if item:
            self.emit('item-activated', item.title, item.line)
    
    def update_structure(self, content):
        """Update the document structure from LaTeX content"""
        sections = self.parse_structure(content)
        
        # Clear existing items
        self.structure_store.remove_all()
        
        # Add new items
        for section in sections:
            self.structure_store.append(section)
    
    def parse_structure(self, content):
        """Parse LaTeX content to extract document structure"""
        sections = []
        
        # Split content into lines for line number tracking
        lines = content.split('\n')
        
        # Regular expressions for different section commands
        section_patterns = [
            # Level 1
            (r'\\chapter\*?{(.*?)}', 1),
            # Level 2
            (r'\\section\*?{(.*?)}', 2),
            # Level 3
            (r'\\subsection\*?{(.*?)}', 3),
            # Level 4
            (r'\\subsubsection\*?{(.*?)}', 4),
            # Level 5
            (r'\\paragraph\*?{(.*?)}', 5)
        ]
        
        # Check for document class to determine if we have chapters
        has_chapters = False
        doc_class_match = re.search(r'\\documentclass.*?{(.*?)}', content)
        if doc_class_match:
            doc_class = doc_class_match.group(1)
            if doc_class in ['book', 'report', 'memoir']:
                has_chapters = True
        
        # If no chapters, adjust levels
        if not has_chapters:
            section_patterns = [(pattern, level-1 if level > 1 else level) for pattern, level in section_patterns]
        
        # Scan document for structure elements
        for line_num, line in enumerate(lines):
            for pattern, level in section_patterns:
                match = re.search(pattern, line)
                if match:
                    title = match.group(1)
                    # Clean up the title (remove LaTeX commands)
                    title = re.sub(r'\\.*?{(.*?)}', r'\1', title)
                    
                    item = StructureItem(title=title, level=level, line=line_num)
                    sections.append(item)
                    break
        
        # Look for other important document elements
        important_elements = [
            (r'\\begin{abstract}', 'Abstract', 2),
            (r'\\begin{figure}', 'Figure', 3),
            (r'\\begin{table}', 'Table', 3),
            (r'\\begin{equation}', 'Equation', 4),
            (r'\\begin{align}', 'Align', 4),
            (r'\\bibliography{', 'Bibliography', 2),
            (r'\\begin{thebibliography}', 'Bibliography', 2),
            (r'\\begin{appendix}', 'Appendix', 2)
        ]
        
        for line_num, line in enumerate(lines):
            for pattern, title, level in important_elements:
                if re.search(pattern, line):
                    # For figures and tables, look for caption
                    if title in ['Figure', 'Table']:
                        caption_pattern = r'\\caption{(.*?)}'
                        # Look in next few lines for caption
                        for i in range(min(10, len(lines) - line_num)):
                            caption_match = re.search(caption_pattern, lines[line_num + i])
                            if caption_match:
                                title = f"{title}: {caption_match.group(1)}"
                                break
                    
                    item = StructureItem(title=title, level=level, line=line_num)
                    sections.append(item)
        
        # Sort by line number
        sections.sort(key=lambda x: x.line)
        
        return sections


class StructureItem(GObject.Object):
    """An item in the document structure"""
    
    def __init__(self, title, level, line):
        super().__init__()
        self.title = title
        self.level = level
        self.line = line
# document_structure.py - LaTeX document structure sidebar component
import re
import gi

gi.require_version('Gtk', '4.0')
from gi.repository import Gtk, GObject, Gdk


class StructureItem:
    """Represents an item in the document structure"""
    
    def __init__(self, title, level, line, text):
        self.title = title
        self.level = level
        self.line = line
        self.text = text


class DocumentStructure(Gtk.Box):
    """Document structure sidebar component"""
    
    __gsignals__ = {
        'item-activated': (GObject.SignalFlags.RUN_FIRST, None, (str, int)),
    }
    
    def __init__(self):
        """Initialize the document structure component"""
        super().__init__(orientation=Gtk.Orientation.VERTICAL)
        
        # Title
        self.header = Gtk.Label(label="Document Structure")
        self.header.set_margin_top(10)
        self.header.set_margin_bottom(10)
        self.header.add_css_class("heading")
        self.append(self.header)
        
        # Create structure tree view
        self.store = Gtk.TreeStore(str, int, int)  # title, line number, level
        self.tree = Gtk.TreeView(model=self.store)
        self.tree.set_headers_visible(False)
        
        # Create column for the tree view
        renderer = Gtk.CellRendererText()
        column = Gtk.TreeViewColumn("Title", renderer, text=0)
        self.tree.append_column(column)
        
        # Create scrolled window for the tree view
        sw = Gtk.ScrolledWindow()
        sw.set_vexpand(True)
        sw.set_child(self.tree)
        self.append(sw)
        
        # Handle selection
        select = self.tree.get_selection()
        select.connect('changed', self.on_selection_changed)
        
        # Set minimum width for sidebar
        self.set_size_request(220, -1)
    
    def update_structure(self, text):
        """Update the document structure from LaTeX text"""
        self.store.clear()
        
        if not text:
            return
        
        # Find all sectioning commands and environments
        sections = self.find_sections(text)
        
        # Fill the tree
        self.fill_structure_tree(sections)
    
    def find_sections(self, text):
        """Find all sections in the LaTeX document"""
        sections = []
        
        # Define the sectioning commands and their levels
        section_commands = {
            'part': 0,
            'chapter': 1,
            'section': 2,
            'subsection': 3,
            'subsubsection': 4,
            'paragraph': 5,
            'subparagraph': 6
        }
        
        # Regular expression for sectioning commands
        section_pattern = r'\\(part|chapter|section|subsection|subsubsection|paragraph|subparagraph)\*?\{([^}]*)\}'
        
        # Find all instances
        for match in re.finditer(section_pattern, text):
            command = match.group(1)
            title = match.group(2)
            
            # Find the line number
            line_number = text[:match.start()].count('\n') + 1
            
            # Get the level
            level = section_commands.get(command, 0)
            
            sections.append(StructureItem(title, level, line_number, match.group(0)))
        
        # Also find environments (e.g., theorems, lemmas)
        env_pattern = r'\\begin\{(theorem|lemma|corollary|definition|example|remark)\}(?:\[[^]]*\])?\s*(?:\{([^}]*)\})?'
        
        for match in re.finditer(env_pattern, text):
            env_type = match.group(1)
            label = match.group(2) or f"{env_type.capitalize()}"
            
            # Find the line number
            line_number = text[:match.start()].count('\n') + 1
            
            # Use a level higher than subsection
            level = 5
            
            sections.append(StructureItem(f"{env_type.capitalize()}: {label}", level, line_number, match.group(0)))
        
        # Sort by line number
        sections.sort(key=lambda x: x.line)
        
        return sections
    
    def fill_structure_tree(self, sections):
        """Fill the tree view with sections"""
        parents = [None] * 10  # Track parent at each level (more than we need)
        
        for section in sections:
            level = section.level
            
            # Find parent based on level
            parent = None
            for i in range(level - 1, -1, -1):
                if parents[i] is not None:
                    parent = parents[i]
                    break
            
            # Add to tree
            iter = self.store.append(parent, [section.title, section.line, level])
            
            # Update parent pointer for this level
            parents[level] = iter
            
            # Clear any deeper level parents
            for i in range(level + 1, len(parents)):
                parents[i] = None
    
    def on_selection_changed(self, selection):
        """Handle selection change in the tree view"""
        model, treeiter = selection.get_selected()
        if treeiter is not None:
            title = model[treeiter][0]
            line = model[treeiter][1]
            self.emit('item-activated', title, line)