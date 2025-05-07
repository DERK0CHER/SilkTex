# Define paths
pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())
moduledir = join_paths(pkgdatadir, 'silktex')

# Import Python module
py_mod = import('python')
python3 = py_mod.find_installation('python3')

# Import the filesystem module
fs = import('fs')

# List of Python source files
silktex_sources = [
  '__init__.py',
  'main.py',
  'window.py',
  'config.py',
  'application.py',
  'latex.py',
  'utils.py',
  'snippets.py',
  'template.py',
  'project.py',
  'biblio.py',
]

# Create package directory structure for proper imports
package_dir = join_paths(moduledir, 'silktex')
# Make sure directory exists but don't use install_dir which doesn't exist
run_command('mkdir', '-p', package_dir, check: false)

# Install Python modules into the silktex package
install_data(silktex_sources, install_dir: package_dir)

# Also install main.py directly to the module dir for direct import
install_data('main.py', install_dir: moduledir)

# Create an __init__.py in the silktex package
configure_file(
  output: '__init__.py',
  command: [python_installation, '-c', 
    'with open("@OUTPUT@", "w") as f: f.write("# SilkTex package\\nfrom .main import main\\n")'],
  install_dir: package_dir
)

# Check if directories exist and create and install those that do
python_directories = ['editor', 'preview', 'compile', 'project']
foreach d : python_directories
  if fs.exists(d)
    install_subdir(d,
      install_dir: moduledir,
      exclude_files: ['meson.build'],
      exclude_directories: ['__pycache__']
    )
  endif
endforeach

# Create constants.py with build info
constants_py = configure_file(
  output: 'constants.py',
  command: ['echo', 'VERSION = "@0@"\nDATADIR = "@1@"\nLOCALEDIR = "@2@"'.format(
    meson.project_version(),
    pkgdatadir,
    join_paths(get_option('prefix'), get_option('localedir'))
  )],
  capture: true,
)
install_data(constants_py, install_dir: moduledir)

# Create a simple __init__.py to make the directory importable
init_py = configure_file(
  output: '__init__.py',
  command: ['echo', '"""SilkTex Package"""'],
  capture: true,
)
install_data(init_py, install_dir: moduledir)

# Configure and install executable script
bin_conf = configuration_data()
bin_conf.set('PYTHON', python3.full_path())
bin_conf.set('pkgdatadir', pkgdatadir)
bin_conf.set('moduledir', moduledir)
bin_conf.set('VERSION', meson.project_version())
bin_conf.set('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

configure_file(
  input: 'silktex.in',
  output: 'silktex',
  configuration: bin_conf,
  install: true,
  install_dir: get_option('bindir')
)

# Process subdirectories that have meson.build files
if fs.exists('editor/meson.build')
  subdir('editor')
endif

if fs.exists('preview/meson.build')
  subdir('preview')
endif

if fs.exists('compile/meson.build')
  subdir('compile')
endif

if fs.exists('project/meson.build')
  subdir('project')
endif