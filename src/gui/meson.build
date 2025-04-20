pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())
moduledir = join_paths(pkgdatadir, 'gummi')
guidir = join_paths(moduledir, 'gui')

# GUI Python modules
gui_sources = [
  'gui-import.py',
  'gui-infoscreen.py',
  'gui-main.py',
  'gui-menu.py',
  'gui-prefs.py',
  'gui-preview.py',
  'gui-project.py',
  'gui-search.py',
  'gui-snippets.py',
  'gui-tabmanager.py',
  'gui_menu.py',
]

# Install GUI modules
install_data(gui_sources, install_dir: guidir)

# Create __init__.py to make the directory a proper Python package
configure_file(
  output: '__init__.py',
  configuration: configuration_data(),
  install: true,
  install_dir: guidir
)