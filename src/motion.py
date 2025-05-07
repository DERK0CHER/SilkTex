import gi
gi.require_version('Gdk', '3.0')
from gi.repository import Gdk

class GuMotion:
    def __init__(self):
        self.key_press_timer = 0
        self.keep_running = False
        self.pause = False
        self.typesetter_pid = 0

    def start(self, user):
        if not user:
            return
        editor = gummi_get_active_editor()
        latex = gummi_get_latex()
        if not (editor and latex):
            return
        self.stop_timer()
        Gdk.threads_enter()
        editortext = latex_update_workfile(editor)
        precompile_ok = latex_precompile_check(editortext)
        Gdk.threads_leave()
        if not precompile_ok:
            gdk_threads_add_idle(on_document_error, "document_error")
            return
        latex_update_pdffile(latex, editor)
        self.typesetter_pid = 0
        if not self.keep_running:
            Gdk.Threads.exit()
        gdk_threads_add_idle(on_document_compiled, editor)
        
    def stop(self):
        self.stop_timer()
        if self.key_press_timer > 0:
            Gdk.source_remove(self.key_press_timer)
            self.key_press_timer = 0
            
    def stop_timer(self):
        if self.key_press_timer > 0:
            Gdk.source_remove(self.key_press_timer)
            self.key_press_timer = 0
            
    def start_timer(self, user):
        self.stop_timer()
        self.key_press_timer = Gdk.timeout_add_seconds(
                                config_get_integer("Compile", "timer"),
                                self.idle_cb, user)
    
    def idle_cb(self, user):
        GuMotion(user).stop_timer()
        if gui.previewgui.preview_on_idle:
            GuMotion().start(Gdk.Threads.add_idle())
        return False
        
    def on_key_press_cb(self, widget, event, user):
        if not event.is_modifier:
            self.stop_timer()
        if config_get_boolean("Interface", "snippets") and \
           snippets_key_press_cb(gummi_get_snippets(), 
                                 gummi_get_active_editor(), event):
            return True
        return False
        
    def on_key_release_cb(self, widget, event, user):
        if not event.is_modifier:
            self.start_timer()
        if config_get_boolean("Interface", "snippets") and \
           snippets_key_release_cb(gummi_get_snippets(), 
                                   gummitor(), event):
            return True
        return False