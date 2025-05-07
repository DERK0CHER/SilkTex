        selected = row.get_selected()
        if selected == 0:
            self.config.set_string("color-scheme", "system")
        elif selected == 1:
            self.config.set_string("color-scheme", "light")
        elif selected == 2:
            self.config.set_string("color-scheme", "dark")
        
        # Apply the change
        style_manager = Adw.StyleManager.get_default()
        if selected == 0:
            style_manager.set_color_scheme(Adw.ColorScheme.DEFAULT)
        elif selected == 1:
            style_manager.set_color_scheme(Adw.ColorScheme.FORCE_LIGHT)
        elif selected == 2:
            style_manager.set_color_scheme(Adw.ColorScheme.FORCE_DARK)
    
    def on_sidebar_visible_changed(self, switch, pspec):
        """Handle sidebar visibility change"""
        active = switch.get_active()
        self.config.set_boolean("sidebar-visible", active)
    
    def on_editor_font_changed(self, button):
        """Handle editor font change"""
        font_desc = button.get_font_desc()
        font_str = font_desc.to_string()
        self.config.set_string("editor-font", font_str)
    
    def on_line_numbers_changed(self, switch, pspec):
        """Handle line numbers visibility change"""
        active = switch.get_active()
        self.config.set_boolean("editor-show-line-numbers", active)
    
    def on_highlight_line_changed(self, switch, pspec):
        """Handle current line highlight change"""
        active = switch.get_active()
        self.config.set_boolean("editor-highlight-current-line", active)
    
    def on_right_margin_changed(self, switch, pspec):
        """Handle right margin visibility change"""
        active = switch.get_active()
        self.config.set_boolean("editor-show-right-margin", active)
    
    def on_margin_position_changed(self, spin):
        """Handle right margin position change"""
        value = spin.get_value_as_int()
        self.config.set_int("editor-right-margin-position", value)
    
    def on_wrap_changed(self, switch, pspec):
        """Handle text wrapping change"""
        active = switch.get_active()
        self.config.set_boolean("editor-wrap-text", active)
    
    def on_auto_indent_changed(self, switch, pspec):
        """Handle auto indent change"""
        active = switch.get_active()
        self.config.set_boolean("editor-auto-indent", active)
    
    def on_use_spaces_changed(self, switch, pspec):
        """Handle use spaces for tabs change"""
        active = switch.get_active()
        self.config.set_boolean("editor-use-spaces", active)
    
    def on_tab_width_changed(self, spin):
        """Handle tab width change"""
        value = spin.get_value_as_int()
        self.config.set_int("editor-tab-width", value)
    
    def on_engine_changed(self, row, pspec):
        """Handle LaTeX engine change"""
        selected = row.get_selected()
        if selected == 0:
            self.config.set_string("latex-engine", "pdflatex")
        elif selected == 1:
            self.config.set_string("latex-engine", "xelatex")
        elif selected == 2:
            self.config.set_string("latex-engine", "lualatex")
    
    def on_shell_escape_changed(self, switch, pspec):
        """Handle shell escape change"""
        active = switch.get_active()
        self.config.set_boolean("latex-shell-escape", active)
    
    def on_auto_compile_changed(self, switch, pspec):
        """Handle auto-compile on save change"""
        active = switch.get_active()
        self.config.set_boolean("latex-auto-compile", active)
    
    def on_auto_refresh_changed(self, switch, pspec):
        """Handle auto-refresh preview change"""
        active = switch.get_active()
        self.config.set_boolean("preview-auto-refresh", active)
    
    def on_refresh_delay_changed(self, spin):
        """Handle refresh delay change"""
        value = spin.get_value_as_int()
        self.config.set_int("preview-refresh-delay", value)
    
    def on_zoom_level_changed(self, spin):
        """Handle zoom level change"""
        value = spin.get_value() / 100.0  # Convert percentage to decimal
        self.config.set_float("preview-zoom-level", value)
