        self.add(page)
    
    def create_editor_page(self):
        """Create the editor preferences page"""
        page = Adw.PreferencesPage(title="Editor", icon_name="document-edit-symbolic")
        
        # Font group
        font_group = Adw.PreferencesGroup(title="Font")
        
        # Font button
        font_button = Gtk.FontButton()
        font_button.set_font(self.config.get_string('editor-font'))
        font_button.set_use_font(True)
        font_button.set_valign(Gtk.Align.CENTER)
        font_button.connect('font-set', self.on_font_set)
        
        font_row = Adw.ActionRow(
            title="Editor Font",
            subtitle="Font used in the editor",
            activatable_widget=font_button
        )
        font_row.add_suffix(font_button)
        font_group.add(font_row)
        
        page.add(font_group)
        
        # Appearance group
        appearance_group = Adw.PreferencesGroup(title="Appearance")
        
        # Line numbers
        line_numbers_switch = Gtk.Switch(
            active=self.config.get_boolean('editor-show-line-numbers'),
            valign=Gtk.Align.CENTER
        )
        line_numbers_switch.connect('notify::active', self.on_line_numbers_changed)
        
        line_numbers_row = Adw.ActionRow(
            title="Show Line Numbers",
            activatable_widget=line_numbers_switch
        )
        line_numbers_row.add_suffix(line_numbers_switch)
        appearance_group.add(line_numbers_row)
        
        # Highlight current line
        highlight_line_switch = Gtk.Switch(
            active=self.config.get_boolean('editor-highlight-current-line'),
            valign=Gtk.Align.CENTER
        )
        highlight_line_switch.connect('notify::active', self.on_highlight_line_changed)
        
        highlight_line_row = Adw.ActionRow(
            title="Highlight Current Line",
            activatable_widget=highlight_line_switch
        )
        highlight_line_row.add_suffix(highlight_line_switch)
        appearance_group.add(highlight_line_row)
        
        # Wrap text
        wrap_text_switch = Gtk.Switch(
            active=self.config.get_boolean('editor-wrap-text'),
            valign=Gtk.Align.CENTER
        )
        wrap_text_switch.connect('notify::active', self.on_wrap_text_changed)
        
        wrap_text_row = Adw.ActionRow(
            title="Wrap Text",
            subtitle="Wrap long lines to fit in the window",
            activatable_widget=wrap_text_switch
        )
        wrap_text_row.add_suffix(wrap_text_switch)
        appearance_group.add(wrap_text_row)
        
        # Right margin
        right_margin_switch = Gtk.Switch(
            active=self.config.get_boolean('editor-show-right-margin'),
            valign=Gtk.Align.CENTER
        )
        right_margin_switch.connect('notify::active', self.on_right_margin_changed)
        
        right_margin_row = Adw.ActionRow(
            title="Show Right Margin",
            activatable_widget=right_margin_switch
        )
        right_margin_row.add_suffix(right_margin_switch)
        appearance_group.add(right_margin_row)
        
        # Right margin position
        margin_adjustment = Gtk.Adjustment(
            value=self.config.get_int('editor-right-margin-position'),
            lower=40,
            upper=200,
            step_increment=1
        )
        margin_spin = Gtk.SpinButton(
            adjustment=margin_adjustment,
            climbs_bars=True,
            snap_to_ticks=True,
            valign=Gtk.Align.CENTER
        )
        margin_spin.connect('value-changed', self.on_margin_position_changed)
        
        margin_position_row = Adw.ActionRow(
            title="Right Margin Position",
            subtitle="Position of the right margin (characters)",
            activatable_widget=margin_spin
        )
        margin_position_row.add_suffix(margin_spin)
        appearance_group.add(margin_position_row)
        
        page.add(appearance_group)
        
        # Indentation group
        indent_group = Adw.PreferencesGroup(title="Indentation")
        
        # Auto indent
        auto_indent_switch = Gtk.Switch(
            active=self.config.get_boolean('editor-auto-indent'),
            valign=Gtk.Align.CENTER
        )
        auto_indent_switch.connect('notify::active', self.on_auto_indent_changed)
        
        auto_indent_row = Adw.ActionRow(
            title="Auto Indent",
            subtitle="Automatically indent new lines to match previous line",
            activatable_widget=auto_indent_switch
        )
        auto_indent_row.add_suffix(auto_indent_switch)
        indent_group.add(auto_indent_row)
        
        # Use spaces
        use_spaces_switch = Gtk.Switch(
            active=self.config.get_boolean('editor-use-spaces'),
            valign=Gtk.Align.CENTER
        )
        use_spaces_switch.connect('notify::active', self.on_use_spaces_changed)
        
        use_spaces_row = Adw.ActionRow(
            title="Insert Spaces Instead of Tabs",
            activatable_widget=use_spaces_switch
        )
        use_spaces_row.add_suffix(use_spaces_switch)
        indent_group.add(use_spaces_row)
        
        # Tab width
        tab_adjustment = Gtk.Adjustment(
            value=self.config.get_int('editor-tab-width'),
            lower=1,
            upper=8,
            step_increment=1
        )
        tab_spin = Gtk.SpinButton(
            adjustment=tab_adjustment,
            climbs_bars=True,
            snap_to_ticks=True,
            valign=Gtk.Align.CENTER
        )
        tab_spin.connect('value-changed', self.on_tab_width_changed)
        
        tab_width_row = Adw.ActionRow(
            title="Tab Width",
            subtitle="Width of a tab character in spaces",
            activatable_widget=tab_spin
        )
        tab_width_row.add_suffix(tab_spin)
        indent_group.add(tab_width_row)
        
        page.add(indent_group)
        
        self.add(page)
    
    def create_latex_page(self):
        """Create the LaTeX preferences page"""
        page = Adw.PreferencesPage(title="LaTeX", icon_name="text-x-generic-symbolic")
        
        # Compilation group
        compile_group = Adw.PreferencesGroup(title="Compilation")
        
        # LaTeX engine selector
        engine_row = Adw.ComboRow(title="LaTeX Engine")
        engine_model = Gtk.StringList()
        engine_model.append("pdflatex")
        engine_model.append("xelatex")
        engine_model.append("lualatex")
        engine_row.set_model(engine_model)
        
        # Set current value
        current_engine = self.config.get_string('latex-engine')
        if current_engine == "xelatex":
            engine_row.set_selected(1)
        elif current_engine == "lualatex":
            engine_row.set_selected(2)
        else:
            engine_row.set_selected(0)
        
        engine_row.connect('notify::selected', self.on_engine_changed)
        compile_group.add(engine_row)
        
        # Shell escape switch
        shell_escape_switch = Gtk.Switch(
            active=self.config.get_boolean('latex-shell-escape'),
            valign=Gtk.Align.CENTER
        )
        shell_escape_switch.connect('notify::active', self.on_shell_escape_changed)
        
        shell_escape_row = Adw.ActionRow(
            title="Enable Shell Escape",
            subtitle="Allow LaTeX to run external commands (potential security risk)",
            activatable_widget=shell_escape_switch
        )
        shell_escape_row.add_suffix(shell_escape_switch)
        compile_group.add(shell_escape_row)
        
        # Auto compile
        auto_compile_switch = Gtk.Switch(
            active=self.config.get_boolean('latex-auto-compile'),
            valign=Gtk.Align.CENTER
        )
        auto_compile_switch.connect('notify::active', self.on_auto_compile_changed)
        
        auto_compile_row = Adw.ActionRow(
            title="Auto Compile on Save",
            activatable_widget=auto_compile_switch
        )
        auto_compile_row.add_suffix(auto_compile_switch)
        compile_group.add(auto_compile_row)
        
        page.add(compile_group)
        
        # Preview group
        preview_group = Adw.PreferencesGroup(title="Preview")
        
        # Auto refresh
        auto_refresh_switch = Gtk.Switch(
            active=self.config.get_boolean('preview-auto-refresh'),
            valign=Gtk.Align.CENTER
        )
        auto_refresh_switch.connect('notify::active', self.on_auto_refresh_changed)
        
        auto_refresh_row = Adw.ActionRow(
            title="Auto Refresh Preview",
            subtitle="Automatically refresh preview when document changes",
            activatable_widget=auto_refresh_switch
        )
        auto_refresh_row.add_suffix(auto_refresh_switch)
        preview_group.add(auto_refresh_row)
        
        # Refresh delay
        delay_adjustment = Gtk.Adjustment(
            value=self.config.get_int('preview-refresh-delay'),
            lower=100,
            upper=5000,
            step_increment=100
        )
        delay_spin = Gtk.SpinButton(
            adjustment=delay_adjustment,
            climbs_bars=True,
            snap_to_ticks=True,
            valign=Gtk.Align.CENTER
        )
        delay_spin.connect('value-changed', self.on_refresh_delay_changed)
        
        delay_row = Adw.ActionRow(
            title="Refresh Delay",
            subtitle="Delay in milliseconds before refreshing preview",
            activatable_widget=delay_spin
        )
        delay_row.add_suffix(delay_spin)
        preview_group.add(delay_row)
        
        page.add(preview_group)
        
        self.add(page)
    
    def on_theme_changed(self, combo_row, pspec):
        """Handle theme preference change"""
        selected = combo_row.get_selected()
        scheme_map = {0: 'system', 1: 'light', 2: 'dark'}
        
        if selected in scheme_map:
            self.config.set_string('color-scheme', scheme_map[selected])
            
            # Apply theme change immediately
            style_manager = Adw.StyleManager.get_default()
            if scheme_map[selected] == 'light':
                style_manager.set_color_scheme(Adw.ColorScheme.FORCE_LIGHT)
            elif scheme_map[selected] == 'dark':
                style_manager.set_color_scheme(Adw.ColorScheme.FORCE_DARK)
            else:
                style_manager.set_color_scheme(Adw.ColorScheme.DEFAULT)
    
    def on_sidebar_default_changed(self, switch, pspec):
        """Handle sidebar visibility preference change"""
        self.config.set_boolean('sidebar-visible', switch.get_active())
    
    def on_font_set(self, font_button):
        """Handle editor font change"""
        font = font_button.get_font()
        self.config.set_string('editor-font', font)
        
        # Update font in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_font(font)
    
    def on_line_numbers_changed(self, switch, pspec):
        """Handle line numbers preference change"""
        self.config.set_boolean('editor-show-line-numbers', switch.get_active())
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_show_line_numbers(switch.get_active())
    
    def on_highlight_line_changed(self, switch, pspec):
        """Handle highlight current line preference change"""
        self.config.set_boolean('editor-highlight-current-line', switch.get_active())
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_highlight_current_line(switch.get_active())
    
    def on_wrap_text_changed(self, switch, pspec):
        """Handle text wrapping preference change"""
        value = switch.get_active()
        self.config.set_boolean('editor-wrap-text', value)
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            if value:
                self.parent.editor_view.set_wrap_mode(Gtk.WrapMode.WORD)
            else:
                self.parent.editor_view.set_wrap_mode(Gtk.WrapMode.NONE)
    
    def on_right_margin_changed(self, switch, pspec):
        """Handle right margin visibility preference change"""
        self.config.set_boolean('editor-show-right-margin', switch.get_active())
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_show_right_margin(switch.get_active())
    
    def on_margin_position_changed(self, spin_button):
        """Handle right margin position preference change"""
        value = spin_button.get_value_as_int()
        self.config.set_int('editor-right-margin-position', value)
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_right_margin(value)
    
    def on_auto_indent_changed(self, switch, pspec):
        """Handle auto indent preference change"""
        self.config.set_boolean('editor-auto-indent', switch.get_active())
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_auto_indent(switch.get_active())
    
    def on_use_spaces_changed(self, switch, pspec):
        """Handle spaces instead of tabs preference change"""
        self.config.set_boolean('editor-use-spaces', switch.get_active())
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_insert_spaces_instead_of_tabs(switch.get_active())
    
    def on_tab_width_changed(self, spin_button):
        """Handle tab width preference change"""
        value = spin_button.get_value_as_int()
        self.config.set_int('editor-tab-width', value)
        
        # Update in current editor
        if hasattr(self.parent, 'editor_view'):
            self.parent.editor_view.set_tab_width(value)
    
    def on_engine_changed(self, combo_row, pspec):
        """Handle LaTeX engine preference change"""
        selected = combo_row.get_selected()
        engines = ["pdflatex", "xelatex", "lualatex"]
        
        if 0 <= selected < len(engines):
            self.config.set_string('latex-engine', engines[selected])
    
    def on_shell_escape_changed(self, switch, pspec):
        """Handle shell escape preference change"""
        self.config.set_boolean('latex-shell-escape', switch.get_active())
    
    def on_auto_compile_changed(self, switch, pspec):
        """Handle auto compile preference change"""
        self.config.set_boolean('latex-auto-compile', switch.get_active())
    
    def on_auto_refresh_changed(self, switch, pspec):
        """Handle auto refresh preview preference change"""
        value = switch.get_active()
        self.config.set_boolean('preview-auto-refresh', value)
        
        # Update in current window
        if hasattr(self.parent, 'auto_refresh_toggle'):
            self.parent.auto_refresh_toggle.set_active(value)
    
    def on_refresh_delay_changed(self, spin_button):
        """Handle preview refresh delay preference change"""
        value = spin_button.get_value_as_int()
        self.config.set_int('preview-refresh-delay', value)
