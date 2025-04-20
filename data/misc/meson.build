pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())

# Configure and install desktop file
desktop_file_config = configuration_data()
desktop_file_config.set('VERSION', meson.project_version())
desktop_file_config.set('BINDIR', join_paths(get_option('prefix'), get_option('bindir')))

desktop_file = configure_file(
  input: 'gummi.desktop.in',
  output: 'gummi.desktop',
  configuration: desktop_file_config,
  install: false
)

# Desktop file installation is handled in parent meson.build

# Install man page (also handled in parent)

# Install default LaTeX template
install_data(
  'default.tex',
  install_dir: join_paths(pkgdatadir, 'templates')
)