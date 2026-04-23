#ifndef __GUMMI_GTK4_COMPAT_H__
#define __GUMMI_GTK4_COMPAT_H__

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#if GTK_MAJOR_VERSION >= 4

typedef struct {
    guint keyval;
    GdkModifierType state;
    guint type;
    gboolean is_modifier;
} GdkEventKey;

typedef struct {
    gdouble x;
    gdouble y;
    GdkModifierType state;
} GdkEventMotion;

typedef struct {
    gdouble x;
    gdouble y;
    GdkModifierType state;
    GdkScrollDirection direction;
} GdkEventScroll;

typedef struct {
    gdouble x;
    gdouble y;
    GdkModifierType state;
    guint type;
} GdkEventButton;

typedef GtkWidget GtkMenuItem;
typedef GtkCheckButton GtkCheckMenuItem;
typedef GtkWidget GtkRadioMenuItem;
typedef GtkToggleButton GtkToggleToolButton;
typedef gpointer GtkAccelGroup;
typedef int GtkAccelFlags;
typedef GdkClipboard GtkClipboard;

#ifndef GTK_MENU_ITEM
#define GTK_MENU_ITEM(widget) ((GtkMenuItem*)(widget))
#endif

#ifndef GTK_CHECK_MENU_ITEM
#define GTK_CHECK_MENU_ITEM(widget) ((GtkCheckMenuItem*)(widget))
#endif

#ifndef GTK_RADIO_MENU_ITEM
#define GTK_RADIO_MENU_ITEM(widget) ((GtkRadioMenuItem*)(widget))
#endif

#ifndef GTK_TOGGLE_TOOL_BUTTON
#define GTK_TOGGLE_TOOL_BUTTON(widget) ((GtkToggleToolButton*)(widget))
#endif

static inline gboolean gtk_check_menu_item_get_active (GtkCheckMenuItem* item) {
    return GTK_IS_CHECK_BUTTON (item)
        ? gtk_check_button_get_active (GTK_CHECK_BUTTON (item))
        : FALSE;
}

static inline void gtk_check_menu_item_set_active (GtkCheckMenuItem* item, gboolean active) {
    if (GTK_IS_CHECK_BUTTON (item)) {
        gtk_check_button_set_active (GTK_CHECK_BUTTON (item), active);
    }
}

static inline const gchar* gtk_menu_item_get_label (GtkMenuItem* item) {
    return GTK_IS_BUTTON (item)
        ? gtk_button_get_label (GTK_BUTTON (item))
        : NULL;
}

static inline void gtk_menu_item_set_label (GtkMenuItem* item, const gchar* label) {
    if (GTK_IS_BUTTON (item)) {
        gtk_button_set_label (GTK_BUTTON (item), label);
    }
}

static inline gboolean gtk_toggle_tool_button_get_active (GtkToggleToolButton* button) {
    return GTK_IS_TOGGLE_BUTTON (button)
        ? gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))
        : FALSE;
}

static inline void gtk_toggle_tool_button_set_active (GtkToggleToolButton* button, gboolean active) {
    if (GTK_IS_TOGGLE_BUTTON (button)) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), active);
    }
}

static inline GtkAccelGroup* gtk_accel_group_new (void) {
    return NULL;
}

static inline void gtk_accel_group_connect (GtkAccelGroup* accel_group, guint accel_key,
        GdkModifierType accel_mods, GtkAccelFlags accel_flags, GClosure* closure) {
    (void)accel_group;
    (void)accel_key;
    (void)accel_mods;
    (void)accel_flags;
    (void)closure;
}

static inline void gtk_accel_group_disconnect (GtkAccelGroup* accel_group, GClosure* closure) {
    (void)accel_group;
    (void)closure;
}

static inline void gtk_window_add_accel_group (GtkWindow* window, GtkAccelGroup* accel_group) {
    (void)window;
    (void)accel_group;
}

static inline void gtk_builder_connect_signals (GtkBuilder* builder, gpointer user_data) {
    (void)builder;
    (void)user_data;
}

static inline gint gtk_dialog_run (GtkDialog* dialog) {
    (void)dialog;
    return GTK_RESPONSE_OK;
}

static inline void gtk_widget_destroy (GtkWidget* widget) {
    if (widget != NULL) {
        gtk_widget_unparent (widget);
    }
}

static inline gint gtk_main (void) {
    return 0;
}

static inline void gtk_main_quit (void) {
}

#ifndef GTK_CONTAINER
#define GTK_CONTAINER(widget) ((GtkWidget*)(widget))
#endif

static inline GList* gtk_container_get_children (GtkWidget* container) {
    GList* children = NULL;
    for (GtkWidget* child = gtk_widget_get_first_child (container);
         child != NULL;
         child = gtk_widget_get_next_sibling (child)) {
        children = g_list_append (children, child);
    }
    return children;
}

static inline void gtk_container_remove (GtkWidget* container, GtkWidget* child) {
    (void)container;
    if (child != NULL) {
        gtk_widget_unparent (child);
    }
}

static inline void gtk_container_add (GtkWidget* container, GtkWidget* child) {
    if (GTK_IS_BOX (container)) {
        gtk_box_append (GTK_BOX (container), child);
        return;
    }
    if (GTK_IS_SCROLLED_WINDOW (container)) {
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (container), child);
        return;
    }
    if (GTK_IS_VIEWPORT (container)) {
        gtk_viewport_set_child (GTK_VIEWPORT (container), child);
        return;
    }
}

static inline void gtk_widget_show_all (GtkWidget* widget) {
    gtk_widget_set_visible (widget, TRUE);
}

static inline const gchar* gtk_entry_get_text (GtkEntry* entry) {
    return gtk_editable_get_text (GTK_EDITABLE (entry));
}

static inline void gtk_entry_set_text (GtkEntry* entry, const gchar* text) {
    gtk_editable_set_text (GTK_EDITABLE (entry), text);
}

static inline gboolean gtk_source_buffer_can_undo (GtkSourceBuffer* buffer) {
    (void)buffer;
    return FALSE;
}

static inline gboolean gtk_source_buffer_can_redo (GtkSourceBuffer* buffer) {
    (void)buffer;
    return FALSE;
}

static inline void gtk_source_buffer_undo (GtkSourceBuffer* buffer) {
    (void)buffer;
}

static inline void gtk_source_buffer_redo (GtkSourceBuffer* buffer) {
    (void)buffer;
}

static inline void gtk_source_buffer_begin_not_undoable_action (GtkSourceBuffer* buffer) {
    gtk_text_buffer_begin_irreversible_action (GTK_TEXT_BUFFER (buffer));
}

static inline void gtk_source_buffer_end_not_undoable_action (GtkSourceBuffer* buffer) {
    gtk_text_buffer_end_irreversible_action (GTK_TEXT_BUFFER (buffer));
}

static inline gboolean gtk_window_get_size (GtkWindow* window, gint* width, gint* height) {
    (void)window;
    if (width) *width = 1200;
    if (height) *height = 800;
    return TRUE;
}

static inline void gtk_window_get_position (GtkWindow* window, gint* x, gint* y) {
    (void)window;
    if (x) *x = 0;
    if (y) *y = 0;
}

static inline void gtk_window_resize (GtkWindow* window, gint width, gint height) {
    gtk_window_set_default_size (window, width, height);
}

static inline void gtk_window_move (GtkWindow* window, gint x, gint y) {
    (void)window;
    (void)x;
    (void)y;
}

#define GTK_WIN_POS_CENTER 0
static inline void gtk_window_set_position (GtkWindow* window, gint position) {
    (void)window;
    (void)position;
}

static inline gboolean gtk_window_set_icon_from_file (GtkWindow* window, const gchar* filename, GError** err) {
    (void)window;
    (void)filename;
    (void)err;
    return TRUE;
}

static inline gboolean gummi_compat_gtk_css_provider_load_from_data (
        GtkCssProvider* provider, const gchar* data, gssize length, GError** error) {
    (void)error;
    gtk_css_provider_load_from_data (provider, data, length);
    return TRUE;
}
#define gtk_css_provider_load_from_data(provider, data, length, error) \
    gummi_compat_gtk_css_provider_load_from_data((provider), (data), (length), (error))

static inline gboolean gummi_compat_gtk_file_chooser_set_current_folder (GtkFileChooser* chooser, const gchar* path) {
    GFile* file = g_file_new_for_path (path);
    gboolean ok = gtk_file_chooser_set_current_folder (chooser, file, NULL);
    g_object_unref (file);
    return ok;
}
#define gtk_file_chooser_set_current_folder(chooser, path) \
    gummi_compat_gtk_file_chooser_set_current_folder((chooser), (path))

static inline gchar* gummi_compat_gtk_file_chooser_get_filename (GtkFileChooser* chooser) {
    GFile* file = gtk_file_chooser_get_file (chooser);
    if (file == NULL) {
        return NULL;
    }
    gchar* path = g_file_get_path (file);
    g_object_unref (file);
    return path;
}
#define gtk_file_chooser_get_filename(chooser) \
    gummi_compat_gtk_file_chooser_get_filename((chooser))

static inline GtkClipboard* gtk_clipboard_get (gpointer selection) {
    (void)selection;
    GdkDisplay* display = NULL;
    if (display == NULL) {
        display = gdk_display_get_default ();
    }
    return gdk_display_get_clipboard (display);
}

#define GDK_SELECTION_CLIPBOARD 0
#define GTK_ACCEL_VISIBLE 0

static inline gboolean gtk_show_uri_on_window (GtkWindow* parent, const gchar* uri, guint32 timestamp, GError** error) {
    (void)parent;
    (void)timestamp;
    (void)error;
    gtk_show_uri (NULL, uri, GDK_CURRENT_TIME);
    return TRUE;
}

typedef GtkWidget GdkWindow;

static inline void gtk_widget_add_events (GtkWidget* widget, gint events) {
    (void)widget;
    (void)events;
}

#define GDK_SCROLL_MASK 0
#define GDK_BUTTON_PRESS_MASK 0
#define GDK_BUTTON_MOTION_MASK 0

static inline GdkWindow* gtk_widget_get_window (GtkWidget* widget) {
    return widget;
}

static inline gint gdk_window_get_scale_factor (GdkWindow* window) {
    (void)window;
    return 1;
}

static inline gdouble gdk_screen_get_resolution (gpointer screen) {
    (void)screen;
    return 96.0;
}

static inline gpointer gdk_screen_get_default (void) {
    return NULL;
}

static inline void gtk_widget_style_get (GtkWidget* widget, const gchar* first_property_name,
        gint* value, gpointer terminator) {
    (void)widget;
    (void)first_property_name;
    (void)terminator;
    if (value) {
        *value = 0;
    }
}

static inline GdkWindow* gtk_viewport_get_view_window (GtkViewport* viewport) {
    return GTK_WIDGET (viewport);
}

static inline gint gdk_window_get_width (GdkWindow* window) {
    (void)window;
    return 1200;
}

static inline gint gdk_window_get_height (GdkWindow* window) {
    (void)window;
    return 800;
}

static inline GtkWidget* gummi_compat_gtk_scrolled_window_new (gpointer hadj, gpointer vadj) {
    (void)hadj;
    (void)vadj;
    return gtk_scrolled_window_new ();
}
#define gtk_scrolled_window_new(hadj, vadj) \
    gummi_compat_gtk_scrolled_window_new((hadj), (vadj))

static inline void gtk_scrolled_window_set_shadow_type (GtkScrolledWindow* sw, gint shadow) {
    (void)sw;
    (void)shadow;
}
#define GTK_SHADOW_OUT 0

static inline void gtk_box_pack_start (GtkBox* box, GtkWidget* child,
        gboolean expand, gboolean fill, guint padding) {
    (void)expand;
    (void)fill;
    (void)padding;
    gtk_box_append (box, child);
}

static inline void gtk_box_pack_end (GtkBox* box, GtkWidget* child,
        gboolean expand, gboolean fill, guint padding) {
    (void)expand;
    (void)fill;
    (void)padding;
    gtk_box_append (box, child);
}

static inline void gtk_widget_set_no_show_all (GtkWidget* widget, gboolean no_show_all) {
    (void)widget;
    (void)no_show_all;
}

static inline void gtk_label_set_line_wrap (GtkLabel* label, gboolean wrap) {
    gtk_label_set_wrap (label, wrap);
}

static inline GtkWidget* gtk_info_bar_get_content_area (GtkInfoBar* bar) {
    GtkWidget* child = gtk_widget_get_first_child (GTK_WIDGET (bar));
    return child != NULL ? child : GTK_WIDGET (bar);
}

static inline GtkWidget* gtk_event_box_new (void) {
    return gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
}

#define GTK_EVENT_BOX(widget) ((GtkWidget*)(widget))
static inline void gtk_event_box_set_visible_window (GtkWidget* box, gboolean visible_window) {
    (void)box;
    (void)visible_window;
}

static inline GtkWidget* gummi_compat_gtk_image_new_from_icon_name (const gchar* icon_name, gint size) {
    (void)size;
    return gtk_image_new_from_icon_name (icon_name);
}
#define gtk_image_new_from_icon_name(icon_name, size) \
    gummi_compat_gtk_image_new_from_icon_name((icon_name), (size))

static inline void gtk_button_set_image (GtkButton* button, GtkWidget* image) {
    gtk_button_set_child (button, image);
}

#define GTK_RELIEF_NONE 0
#define GTK_ICON_SIZE_MENU 0
#define GTK_ICON_LOOKUP_GENERIC_FALLBACK 0

#endif /* GTK_MAJOR_VERSION >= 4 */

#endif /* __GUMMI_GTK4_COMPAT_H__ */
