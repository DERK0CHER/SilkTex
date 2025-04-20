pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())

# Desktop file
# Desktop file
desktop_file = i18n.merge_file(
  input: 'misc/gummi.desktop.in',
  output: 'com.github.gummi_editor.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

# Validate desktop file
desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils,
    args: [desktop_file]
  )
endif

# Install man pages
install_man('misc/gummi.1')

# Install default template
install_data('misc/default.tex', install_dir: join_paths(pkgdatadir, 'templates'))

# Install UI files
install_data(
  'ui/gummi.ui',
  'ui/main.ui',
  'ui/prefs.glade',
  'ui/snippets.ui',
  install_dir: join_paths(pkgdatadir, 'ui')
)

# Process subdirectories
subdir('icons')
subdir('snippets')