project('silktex', 
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.59.0',
)

python = import('python')
i18n = import('i18n')
gnome = import('gnome')

# Python installation check
python_installation = python.find_installation('python3', required: true)

# Check Python modules
python_check = run_command(python_installation,
  '-c', 'import gi; print("Python modules found")',
  check: false
)

# Check for necessary dependencies
adw_dep = dependency('libadwaita-1', version: '>= 1.0.0', required: true)
gtk_dep = dependency('gtk4', version: '>= 4.0.0', required: true)
webkit_dep = dependency('webkit2gtk-6.0', required: false)
if not webkit_dep.found()
  webkit_dep = dependency('webkitgtk-6.0', required: true)
endif

# Try both package names for gtksourceview-5
gtksourceview_dep = dependency('gtksourceview-5', version: '>= 5.0.0', required: false)
if not gtksourceview_dep.found()
  # Try with the package name used in some distributions
  gtksourceview_dep = dependency('gtksourceview5', version: '>= 5.0.0', required: false)
  if not gtksourceview_dep.found()
    # Try with pkgconfig directory hint
    gtksourceview_dep = dependency('gtksourceview-5',
                                  version: '>= 5.0.0',
                                  required: true,
                                  method: 'pkg-config')
  endif
endif

# Add all dependencies to a single array for easier use
deps = [
  adw_dep,
  gtk_dep,
  gtksourceview_dep,
  webkit_dep,
]

# Configure data
conf = configuration_data()
conf.set('VERSION', meson.project_version())
conf.set('PACKAGE_NAME', meson.project_name())
conf.set('PACKAGE_VERSION', meson.project_version())
conf.set('PREFIX', get_option('prefix'))
conf.set('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
conf.set('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

pkgdatadir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())

# Include subdirectories
subdir('data')
subdir('src')

# Add po directory conditionally to avoid errors with missing files
if run_command('test', '-d', join_paths(meson.current_source_dir(), 'po'), check: false).returncode() == 0
  message('po directory found, including it in build')
  subdir('po')
else
  message('po directory not properly set up, skipping')
endif

# Post install script - only need this once
meson.add_install_script('meson-post-install.py')

# GNOME post-install actions
gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)

# Install application icon
install_data('data/icons/icon.png',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps')
)

# Install app data file
install_data('data/org.example.silktex.appdata.xml',
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

# Generate and install man page
manpage = configure_file(
  output: 'silktex.1',
  command: [python_installation, '-c', 
    'with open("@OUTPUT@", "w") as f: f.write(".TH SILKTEX 1 \\"' + meson.project_version() + '\\" \\"SilkTex Manual\\"\\n.SH NAME\\nsilktex \\\\- LaTeX editor with live preview\\n.SH SYNOPSIS\\n.B silktex\\n[\\\\fIFILE\\\\fR]\\n.SH DESCRIPTION\\n.B SilkTex\\nis a LaTeX editor with live preview functionality.\\n.SH AUTHORS\\nSilkTex developers")'],
  capture: false,
)
install_data(manpage, install_dir: join_paths(get_option('mandir'), 'man1'))
project('silktex',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 0.59.0',
)

# Project options
python = import('python')
py_installation = python.find_installation('python3')

# Dependencies - check for both gtksourceview-5 and gtksourceview5
# as package names can vary between distributions
deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.0.0'),
]

# Check for gtksourceview5 with both common package naming schemes
gtksource_dep = dependency('gtksourceview-5', required: false)
if not gtksource_dep.found()
  gtksource_dep = dependency('gtksourceview5', required: true)
endif

# Python module dependencies
py_deps = [
  'PyGObject>=3.42.0',
  'pycairo>=1.20.0',
  'pypdf>=3.0.0',
  'pyenchant>=3.2.0',
  'watchdog>=2.1.0',
]

# Set program name and application ID
app_id = 'org.silktex.SilkTex'
prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
pkgdatadir = join_paths(datadir, meson.project_name())

# Install Python files
py_installation.install_sources([
  'src/silktex/main.py',
  'src/silktex/app.py',
  'src/silktex/window.py',
  'src/silktex/editor.py',
  'src/silktex/preview.py',
  'src/silktex/latex.py',
  'src/silktex/templates.py',
  'src/silktex/snippets.py',
  'src/silktex/config.py',
  'src/silktex/project.py',
  'src/silktex/spell_checker.py',
  'src/silktex/stats.py',
], subdir: 'silktex')

# Install application resources
install_data('data/org.silktex.SilkTex.desktop', install_dir: join_paths(datadir, 'applications'))
install_data('data/org.silktex.SilkTex.appdata.xml', install_dir: join_paths(datadir, 'metainfo'))
install_data('data/org.silktex.SilkTex.gschema.xml', install_dir: join_paths(datadir, 'glib-2.0', 'schemas'))

# Install icons
icondir = join_paths(datadir, 'icons', 'hicolor')
install_data('data/icons/org.silktex.SilkTex.svg', install_dir: join_paths(icondir, 'scalable', 'apps'))
install_data('data/icons/org.silktex.SilkTex-symbolic.svg', install_dir: join_paths(icondir, 'symbolic', 'apps'))

# Install UI files
install_data(
  'data/ui/window.ui',
  'data/ui/preferences.ui',
  'data/ui/template_dialog.ui',
  'data/ui/snippet_dialog.ui',
  install_dir: join_paths(pkgdatadir, 'ui')
)

# Install LaTeX templates
install_subdir('data/templates', install_dir: pkgdatadir)

# Install syntax highlighting files
install_subdir('data/language-specs', install_dir: pkgdatadir)

# Compile schemas
meson.add_install_script('build-aux/meson/postinstall.py')

# Create executable shell script
configure_file(
  input: 'src/silktex.in',
  output: 'silktex',
  configuration: {
    'PYTHON': py_installation.path(),
    'PREFIX': prefix,
  },
  install_dir: bindir
)
# Install templates and resources
install_subdir('data/templates',
  install_dir: join_paths(get_option('datadir'), 'silktex')
)

# Summary
summary = [
  '',
  '------',
  'SilkTex @0@'.format(meson.project_version()),
  '',
  'Prefix:        @0@'.format(get_option('prefix')),
  'Data dir:      @0@'.format(join_paths(get_option('prefix'), get_option('datadir'))),
  'GTK4:          @0@'.format(gtk_dep.found() ? 'Yes' : 'No'),
  'Libadwaita:    @0@'.format(adw_dep.found() ? 'Yes' : 'No'),
  'GtkSourceView: @0@'.format(gtksourceview_dep.found() ? 'Yes' : 'No'),
  'WebKit:        @0@'.format(webkit_dep.found() ? 'Yes' : 'No'),
  '------',
]

# Print summary information
message('\n'.join(summary))