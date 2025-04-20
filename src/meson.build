pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())
moduledir = join_paths(pkgdatadir, 'gummi')

# Python sources
python_sources = [
  'biblio.py',
  'configfile.py',
  'constants.py',
  'editor.py',
  'environment.py',
  'external.py',
  'importer.py',
  'iofunctions.py',
  'latex.py',
  'main.py',
  'motion.py',
  'project.py',
  'signals.py',
  'snippets.py',
  'tabmanager.py',
  'template.py',
  'utils.py',
]

# Install Python modules
install_data(python_sources, install_dir: moduledir)

# Configure constants.py with build information
constants_conf = configuration_data()
constants_conf.set('VERSION', meson.project_version())
constants_conf.set('DATADIR', pkgdatadir)
constants_conf.set('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

configure_file(
  input: 'constants.py',
  output: 'constants.py',
  configuration: constants_conf,
  install: true,
  install_dir: moduledir
)

# Create executable script
bin_conf = configuration_data()
bin_conf.set('PYTHON', python.find_installation('python3').path())
bin_conf.set('pkgdatadir', pkgdatadir)

configure_file(
  input: 'main.py',
  output: 'gummi',
  configuration: bin_conf,
  install: true,
  install_dir: get_option('bindir')
)

# Install typesetters
install_data('compile/rubber.py', install_dir: join_paths(moduledir, 'compile'))
install_data('compile/latexmk.py', install_dir: join_paths(moduledir, 'compile'))
install_data('compile/texlive.py', install_dir: join_paths(moduledir, 'compile'))

# Install GUI modules
subdir('gui')