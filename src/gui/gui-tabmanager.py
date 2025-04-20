import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, GtkSource, Gdk

class GuTabmanagerGui:
    def __init__(self, builder):
        if not isinstance(builder, Gtk.Builder):
            raise ValueError("Builder argument must be a Gtk.Builder instance")
            
        self.notebook = builder.get_object("tab_notebook")
        self.unsavednr = 0

class GuTabPage:
    def __init__(self):
        self.scrollw = None
        self.labelbox = None
        self.label = None
        self.button = None
        self.editorbox = None
        self.infobar = None
        self.barlabel = None
        self.unsavednr = 0
        self.bold = False

def create_page(tc, editor):
    tp = GuTabPage()
    tc.page = tp
    
    # Create scrolled window
    tp.scrollw = Gtk.ScrolledWindow()
    tp.scrollw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
    
    # Create label
    labeltext = tc.get_tabname()  # Assuming this method exists in TabContext
    create_label(tp, labeltext)
    tp.button.connect("clicked", on_menu_close_activate, tc)
    
    # Create infobar
    create_infobar(tp)
    
    # Add editor view to scrolled window
    tp.scrollw.set_child(editor.view)
    
    # Create main box for the tab
    tp.editorbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
    tp.editorbox.append(tp.infobar)
    tp.editorbox.append(tp.scrollw)
    
    # Add page to notebook
    pos = g_tabnotebook.append_page(tp.editorbox, tp.labelbox)
    tp.editorbox.show()
    
    return pos

def create_infobar(tp):
    infobar = Gtk.InfoBar()
    infobar.set_can_focus(False)
    
    message = Gtk.Label()
    message.set_wrap(True)
    message.show()
    
    content_area = infobar.get_content_area()
    content_area.append(message)
    
    infobar.add_button("_Yes", Gtk.ResponseType.YES)
    infobar.add_button("_No", Gtk.ResponseType.NO)
    infobar.set_message_type(Gtk.MessageType.WARNING)
    
    tp.barlabel = message
    tp.infobar = infobar

def create_label(tp, labeltext):
    global count
    count = getattr(create_label, 'count', 0) + 1
    create_label.count = count
    
    tp.labelbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
    tp.unsavednr = count
    
    tp.label = Gtk.Label(label=labeltext)
    tp.labelbox.append(tp.label)
    
    tp.button = Gtk.Button()
    image = Gtk.Image.new_from_icon_name("window-close")
    tp.button.set_child(image)
    tp.button.set_has_frame(False)
    tp.button.set_can_focus(False)
    
    tp.labelbox.append(tp.button)
    tp.labelbox.show()

def get_labeltext(tp):
    return tp.label.get_text()

def replace_page(tc, new_editor):
    active_tab = gummi.tabmanager.active_tab
    active_tab.editor = new_editor
    
    # Remove old editor view
    tc.page.scrollw.get_child().unparent()
    # Add new editor view
    tc.page.scrollw.set_child(new_editor.view)
    new_editor.view.show()
    
    return g_tabnotebook.page_num(active_tab.page.editorbox)

def set_current_page(position):
    g_tabnotebook.set_current_page(position)

def get_current_page():
    return g_tabnotebook.get_current_page()

def get_n_pages():
    return g_tabnotebook.get_n_pages()

def update_label(tp, text):
    if tp is None:
        return
    tp.label.set_text(text)
    if tp.bold:
        set_bold_text(tp)

def set_bold_text(tp):
    current_text = tp.label.get_text()
    markup = f"<span weight='bold'>{current_text}</span>"
    tp.label.set_markup(markup)
    if not tp.bold:
        tp.bold = True