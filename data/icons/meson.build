pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())
icondir = join_paths(get_option('prefix'), get_option('datadir'), 'icons')

# Install application icon to hicolor icon theme
install_data(
  'gummi.png',
  'icon.png',
  install_dir: join_paths(pkgdatadir, 'icons')
)

# Install app icon to the hicolor icon theme
install_data(
  'gummi.png',
  install_dir: join_paths(icondir, 'hicolor', '48x48', 'apps'),
  rename: 'com.github.gummi_editor.png'
)

# Install Windows icon if needed
if host_machine.system() == 'windows'
  install_data(
    'gummi.ico',
    install_dir: pkgdatadir
  )
endif