import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
gi.require_version('Poppler', '0.18')
from gi.repository import Gtk, GtkSource, Gio, GLib, Gdk, Poppler, cairo
import math
import os
import sys
from enum import IntEnum
import ctypes
import tempfile

# Constants
DOCUMENT_MARGIN = 20
PAGE_MARGIN = 10
PAGE_SHADOW_WIDTH = 5
PAGE_SHADOW_OFFSET = 5
BYTES_PER_PIXEL = 4
ASCROLL_STEPS = 25
ASCROLL_CONST_A = 2.0
ASCROLL_CONST_B = -3.0
ASCROLL_CONST_C = 1.0

class ZoomSizes(IntEnum):
    FIT_BOTH = 0
    FIT_WIDTH = 1
    ZOOM_50 = 2
    ZOOM_70 = 3
    ZOOM_85 = 4
    ZOOM_100 = 5
    ZOOM_125 = 6
    ZOOM_150 = 7
    ZOOM_200 = 8
    ZOOM_300 = 9
    ZOOM_400 = 10
    N_ZOOM_SIZES = 11

class FitMode(IntEnum):
    NUMERIC = 0
    WIDTH = 1
    BOTH = 2

class PreviewPage:
    def __init__(self):
        self.width = 0
        self.height = 0
        self.rendering = None

class LayeredRectangle:
    def __init__(self):
        self.x = 0
        self.y = 0
        self.width = 0
        self.height = 0
        self.layer = 0

class PageLayout:
    def __init__(self):
        self.inner = LayeredRectangle()
        self.outer = LayeredRectangle()

class SyncNode:
    def __init__(self):
        self.page = 0
        self.x = 0
        self.y = 0
        self.width = 0
        self.height = 0
        self.score = 0

class PreviewGui:
    def __init__(self, builder):
        self.scrollw = builder.get_object("preview_scrollw")
        self.viewport = builder.get_object("preview_vport")
        self.drawarea = builder.get_object("preview_draw")
        self.toolbar = builder.get_object("preview_toolbar")
        
        self.combo_sizes = builder.get_object("combo_preview_size")
        self.model_sizes = builder.get_object("model_preview_size")
        
        self.page_next = builder.get_object("page_next")
        self.page_prev = builder.get_object("page_prev")
        self.page_label = builder.get_object("page_label")
        self.page_input = builder.get_object("page_input") 
        self.preview_pause = builder.get_object("preview_pause")
        
        self.page_layout_single_page = builder.get_object("page_layout_single_page")
        self.page_layout_one_column = builder.get_object("page_layout_one_column")
        
        # Initialize variables
        self.uri = None
        self.doc = None
        self.current_page = 0
        self.n_pages = 0
        self.pages = []
        self.scale = 1.0
        self.prev_x = 0
        self.prev_y = 0
        self.cache_size = 0
        self.width_pages = 0
        self.height_pages = 0
        self.width_scaled = 0
        self.height_scaled = 0
        self.update_timer = 0
        self.preview_on_idle = False
        self.errormode = False
        self.sync_nodes = []
        
        # Scrolled window adjustments
        self.hadj = self.scrollw.get_hadjustment()
        self.vadj = self.scrollw.get_vadjustment()
        
        # Setup events
        self.drawarea.add_events(
            Gdk.EventMask.SCROLL_MASK | 
            Gdk.EventMask.BUTTON_PRESS_MASK |
            Gdk.EventMask.BUTTON_MOTION_MASK
        )
        
        # Connect signals
        self.connect_signals()
        
        # Initialize zoom levels with HiDPI/Retina support
        self.init_zoom_levels()

    def connect_signals(self):
        self.page_input_changed_handler = self.page_input.connect(
            "changed", self.on_page_input_changed)
        self.page_input.connect(
            "focus-out-event", self.on_page_input_lost_focus)
        self.combo_sizes_changed_handler = self.combo_sizes.connect(
            "changed", self.on_combo_sizes_changed)
        self.page_prev.connect("clicked", self.on_prev_page_clicked)
        self.page_next.connect("clicked", self.on_next_page_clicked)
        self.scrollw.connect("size-allocate", self.on_resize)
        self.drawarea.connect("draw", self.on_draw)
        self.drawarea.connect("scroll-event", self.on_scroll)
        self.drawarea.connect("button-press-event", self.on_button_pressed)
        self.drawarea.connect("motion-notify-event", self.on_motion)
        
        # Adjustment handlers
        self.hadj.connect("value-changed", self.on_adj_changed)
        self.vadj.connect("value-changed", self.on_adj_changed)
        self.hadj.connect("changed", self.on_adj_changed)
        self.vadj.connect("changed", self.on_adj_changed)

    def init_zoom_levels(self):
        self.list_sizes = [-1, -1, 0.50, 0.70, 0.85, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0]
        
        # Apply HiDPI scaling
        scale_factor = self.get_scale_factor()
        screen_dpi = self.get_screen_dpi()
        
        if screen_dpi == -1:
            screen_dpi = 96.0
            
        poppler_scale = (screen_dpi / 72.0) * scale_factor
        
        for i in range(len(self.list_sizes)):
            self.list_sizes[i] *= poppler_scale

    def get_scale_factor(self):
        if hasattr(self.drawarea, 'get_scale_factor'):
            return self.drawarea.get_scale_factor()
        return 1.0

    def get_screen_dpi(self):
        display = Gdk.Display.get_default()
        monitor = display.get_primary_monitor()
        if monitor:
            return monitor.get_geometry().height * 25.4 / monitor.get_height_mm()
        return 96.0

    # Core preview functionality methods follow...
# ... continuing from previous code ...

    def set_pdffile(self, uri):
        """Set and load a new PDF file for preview"""
        self.cleanup_fds()
        
        self.uri = uri
        try:
            self.doc = Poppler.Document.new_from_file(self.uri, None)
        except GLib.Error as e:
            print(f"Error loading PDF: {e.message}")
            return
            
        self.load_document(False)
        
        # Clear sync nodes
        self.clear_sync_nodes()
        
        # Restore positions
        self.restore_position()
        
        # Update zoom/scale
        self.update_zoom_mode_from_config()
        
        self.drawarea.queue_draw()
        self.goto_page(0)

    def load_document(self, update=False):
        """Load or reload the current document"""
        self.invalidate_renderings()
        self.pages.clear()
        
        if not self.doc:
            return
            
        self.n_pages = self.doc.get_n_pages()
        self.page_label.set_text(f"of {self.n_pages}")
        
        for i in range(self.n_pages):
            page = PreviewPage()
            poppler_page = self.doc.get_page(i)
            page.width, page.height = poppler_page.get_size()
            self.pages.append(page)
            
        self.update_page_sizes()
        self.update_prev_next_page()

    def get_page_rendering(self, page_num):
        """Get or create rendered page surface"""
        if page_num < 0 or page_num >= self.n_pages:
            return None
            
        page = self.pages[page_num]
        if page.rendering is None:
            poppler_page = self.doc.get_page(page_num)
            page.rendering = self.do_render(poppler_page, page.width, page.height)
            self.cache_size += (page.width * page.height * BYTES_PER_PIXEL)
            
            # Trigger garbage collection if needed
            GLib.idle_add(self.run_garbage_collector)
            
        return page.rendering

    def do_render(self, poppler_page, width, height):
        """Render a page to a cairo surface"""
        if not poppler_page:
            return None
            
        user_scale = self.scale
        dpi_scale = self.get_scale_factor()
        
        surface_width = int(width * user_scale * dpi_scale)
        surface_height = int(height * user_scale * dpi_scale)
        
        surface = cairo.ImageSurface(
            cairo.Format.ARGB32,
            surface_width,
            surface_height
        )
        
        ctx = cairo.Context(surface)
        
        # White background
        ctx.set_source_rgb(1, 1, 1)
        ctx.paint()
        
        # Scale and render PDF
        ctx.scale(user_scale * dpi_scale, user_scale * dpi_scale)
        poppler_page.render(ctx)
        
        return surface

    def paint_page(self, cr, page_num, x, y):
        """Paint a page with shadow and border"""
        if page_num < 0 or page_num >= self.n_pages:
            return
            
        page_width = self.get_page_width(page_num) * self.scale
        page_height = self.get_page_height(page_num) * self.scale
        
        # Draw shadow
        cr.set_source_rgb(0.302, 0.302, 0.302)
        cr.rectangle(x + page_width, y + PAGE_SHADOW_OFFSET,
                    PAGE_SHADOW_WIDTH, page_height)
        cr.fill()
        cr.rectangle(x + PAGE_SHADOW_OFFSET, y + page_height,
                    page_width - PAGE_SHADOW_OFFSET, PAGE_SHADOW_WIDTH)
        cr.fill()
        
        # Draw border
        cr.set_line_width(0.5)
        cr.set_source_rgb(0, 0, 0)
        cr.rectangle(x - 1, y - 1, page_width + 1, page_height + 1)
        cr.stroke()
        
        # Draw page content
        rendering = self.get_page_rendering(page_num)
        if rendering:
            device_scale = self.get_scale_factor()
            
            cr.save()
            cr.scale(1.0 / device_scale, 1.0 / device_scale)
            cr.set_source_surface(rendering, x * device_scale, y * device_scale)
            cr.paint()
            cr.restore()
            
        # Draw sync nodes if in debug mode
        if self.in_debug_mode():
            self.draw_sync_nodes(cr, page_num, x, y)

    def on_draw(self, widget, cr):
        """Handle draw events for the preview area"""
        if not self.uri or not os.path.exists(self.uri.replace('file://', '')):
            return False
            
        page_width = self.hadj.get_page_size()
        page_height = self.vadj.get_page_size()
        
        offset_x = max(self.get_document_margin(),
                      (page_width - self.width_scaled) / 2)
        
        if self.is_continuous():
            # Continuous mode rendering
            offset_y = max(self.get_document_margin(),
                         (page_height - self.height_scaled) / 2)
            
            view_start_y = self.vadj.get_value() - self.get_page_margin()
            view_end_y = view_start_y + page_height + 2 * self.get_page_margin()
            
            # Find first visible page
            i = 0
            while i < self.n_pages:
                offset_y += self.get_page_height(i) * self.scale + self.get_page_margin()
                if offset_y >= view_start_y:
                    break
                i += 1
                
            # Adjust offset
            offset_y -= self.get_page_height(i) * self.scale + self.get_page_margin()
            
            # Paint visible pages
            while i < self.n_pages:
                self.paint_page(cr, i,
                    self.page_offset_x(i, offset_x),
                    self.page_offset_y(i, offset_y))
                    
                offset_y += self.get_page_height(i) * self.scale + self.get_page_margin()
                
                if offset_y > view_end_y:
                    break
                i += 1
        else:
            # Single page mode
            height = self.get_page_height(self.current_page) * self.scale
            offset_y = max(self.get_document_margin(), (page_height - height) / 2)
            
            self.paint_page(cr, self.current_page,
                self.page_offset_x(self.current_page, offset_x),
                self.page_offset_y(self.current_page, offset_y))
                
        return True

    def goto_page(self, page):
        """Navigate to specific page"""
        page = max(0, min(page, self.n_pages - 1))
        self.set_current_page(page)
        
        y = 0
        if not self.is_continuous():
            self.update_scaled_size()
            self.update_drawarea_size()
        else:
            for i in range(page):
                y += self.get_page_height(i) * self.scale + self.get_page_margin()
                
        self.goto_xy(self.hadj.get_value(), self.vadj.get_value())
        
        if not self.is_continuous():
            self.drawarea.queue_draw()

    def goto_xy(self, x, y):
        """Scroll to specific coordinates"""
        if math.isnan(x) or math.isnan(y):
            return
            
        x = max(0, min(x, self.hadj.get_upper() - self.hadj.get_page_size()))
        y = max(0, min(y, self.vadj.get_upper() - self.vadj.get_page_size()))
        
        self.block_handlers_current_page()
        
        self.hadj.set_value(x)
        self.vadj.set_value(y)
        self.save_position()
        
        self.unblock_handlers_current_page()

    # Helper methods
    def is_continuous(self):
        """Check if in continuous page layout mode"""
        return self.page_layout == Poppler.PageLayout.ONE_COLUMN

    def get_document_margin(self):
        """Get document margin based on layout"""
        return 0 if self.page_layout == Poppler.PageLayout.SINGLE_PAGE else DOCUMENT_MARGIN

    def get_page_margin(self):
        """Get margin between pages"""
        return PAGE_MARGIN

    def get_page_height(self, page):
        """Get height of specific page"""
        if 0 <= page < self.n_pages:
            return self.pages[page].height
        return -1

    def get_page_width(self, page):
        """Get width of specific page"""
        if 0 <= page < self.n_pages:
            return self.pages[page].width
        return -1
        
        