    def on_about_action(self, widget, _):
        about = Adw.AboutWindow(transient_for=self.props.active_window,
                                application_name='SilkTex',
                                application_icon='document-edit-symbolic',
                                developer_name='Developer',
                                version='0.1.0',
                                developers=['Your Name'],
                                copyright='© 2023 Your Name')
        about.present()
