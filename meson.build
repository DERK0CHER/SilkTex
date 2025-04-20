project('gummi', 'c',
  version: '0.8.3',
  license: 'MIT',
  meson_version: '>= 0.59.0',
  default_options: [
    'warning_level=2',
  ],
)
python = import('python')
i18n = import('i18n')
gnome = import('gnome')

# Check for Python dependencies
py_installation = python.find_installation('python3')

# Alternative method to check for Python modules
python_check = run_command(py_installation,
  '-c', 'import gi; print("Python modules found")',
  check: false
)

# GObject introspection dependencies
dependency('gtk4', version: '>= 4.0.0')
dependency('gtksourceview-5', version: '>= 5.0.0')

# Configure data
conf = configuration_data()
conf.set('VERSION', meson.project_version())
conf.set('PACKAGE_NAME', meson.project_name())
conf.set('PACKAGE_VERSION', meson.project_version())
conf.set('PREFIX', get_option('prefix'))
conf.set('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
conf.set('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

subdir('data')
subdir('src')
subdir('po')

# Post install script
meson.add_install_script('meson-post-install.py')

# Summary
summary = [
  '',
  '------',
  'Gummi @0@'.format(meson.project_version()),
  '',
  'Prefix:        @0@'.format(get_option('prefix')),
  'Data dir:      @0@'.format(join_paths(get_option('prefix'), get_option('datadir'))),
  'Compiler:      @0@'.format(meson.get_compiler('c').get_id()),
  '------',
]

message('\n'.join(summary))