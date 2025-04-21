pkgdatadir = get_option('prefix') / get_option('datadir') / meson.project_name()
moduledir = pkgdatadir / 'silktex'
gnome = import('gnome')

# Use ui_targets instead of ui_files for dependencies
# ui_targets contains the actual CustomTarget objects
gnome.compile_resources('silktex',
  'silktex.gresource.xml',
  gresource_bundle: true,
  install: true,
  install_dir: pkgdatadir,
  dependencies: ui_targets  # Use ui_targets here instead of ui_files
)

python = import('python')

conf = configuration_data()
conf.set('PYTHON', python.find_installation('python3').full_path())
conf.set('VERSION', meson.project_version())
conf.set('localedir', get_option('prefix') / get_option('localedir'))
conf.set('pkgdatadir', pkgdatadir)

configure_file(
  input: 'silktex.in',
  output: 'silktex',
  configuration: conf,
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'r-xr-xr-x'
)

silktex_sources = [
  '__init__.py',
  'main.py',
  'window.py',
  'layout.py'
]

install_data(silktex_sources, install_dir: moduledir)