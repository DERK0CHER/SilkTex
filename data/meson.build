pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())

# Install desktop file directly instead of using i18n.merge_file 
# since we don't have translation files in the po directory
desktop_file = configure_file(
  input: 'org.example.silktex.desktop.in',
  output: 'org.example.silktex.desktop',
  copy: true
)
install_data(desktop_file, install_dir: join_paths(get_option('datadir'), 'applications'))

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils,
    args: [desktop_file]
  )
endif

# Use existing appdata file directly instead of trying to merge a non-existent input file
install_data('org.example.silktex.appdata.xml',
  install_dir: join_paths(get_option('datadir'), 'appdata')
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
  test('Validate appstream file', appstream_util,
    args: ['validate', join_paths(meson.current_source_dir(), 'org.example.silktex.appdata.xml')]
  )
endif

install_data('org.example.silktex.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test('Validate schema file', compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()]
  )
endif

# Compile GResources
gnome = import('gnome')
gnome.compile_resources('silktex',
  'resources/silktex.gresource.xml',
  gresource_bundle: true,
  install: true,
  install_dir: pkgdatadir,
)

# Process subdirectories
subdir('templates')
subdir('icons')
subdir('resources')

# Desktop file validation is already handled above

# AppStream metadata (metainfo format)
if run_command('test', '-f', join_paths(meson.current_source_dir(), 'org.example.silktex.metainfo.xml.in'), check: false).returncode() == 0
  install_data('org.example.silktex.metainfo.xml.in',
    rename: 'org.example.silktex.metainfo.xml',
    install_dir: join_paths(get_option('datadir'), 'metainfo')
  )

  appstream_util = find_program('appstream-util', required: false)
  if appstream_util.found()
    test('Validate metainfo file', appstream_util, 
      args: ['validate', join_paths(meson.current_source_dir(), 'org.example.silktex.metainfo.xml.in')])
  endif
endif

# GSchema
install_data('org.example.silktex.gschema.xml',
  rename: 'org.example.silktex.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test('Validate schema file',
       compile_schemas,
       args: ['--strict', '--dry-run', meson.current_source_dir()])
endif

# Install UI files
install_data(
  'ui/main_window.ui',
  'ui/editor_view.ui',
  'ui/preview_view.ui',
  'ui/preferences.ui',
  'ui/template_dialog.ui',
  'ui/project_sidebar.ui',
  'ui/document_statistics.ui',
  install_dir: join_paths(pkgdatadir, 'ui')
)

# Templates and Snippets
install_subdir(
  'templates',
  install_dir: pkgdatadir
)

install_subdir(
  'snippets',
  install_dir: pkgdatadir
)
