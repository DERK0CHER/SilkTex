project(
    'gummi',
    'c',
    version: '0.8.3',
    license: 'MIT',
    meson_version: '>=0.50.0'
)

i18n = import('i18n')


#dependency('setconf')

prefix = get_option('prefix')
bindir = get_option('bindir')
datadir = get_option('datadir')
libdir = get_option('libdir')
localesdir = get_option('localedir')
appdir = join_paths(datadir, 'applications')
policydir = join_paths(datadir, 'polkit-1/actions')
icondir = join_paths(datadir, 'icons/hicolor/')
schemadir = join_paths(datadir, 'glib-2.0/schemas')
mandir = join_paths(datadir, 'man/man1/')
gummidir = join_paths(datadir, meson.project_name())

gtkspell_dep = dependency('gtkspell3-3.0', required: false)

project_dependencies = [
    dependency('gtk4'),
    dependency('libadwaita-1'),
    dependency('glib-2.0'),
    dependency('gtksourceview-5'),
    gtkspell_dep,
    dependency('poppler-glib'),
    dependency('synctex'),
]

if gtkspell_dep.found()
  add_project_arguments(['-DHAVE_GTKSPELL3=1'], language: 'c')
endif

build_args = [
  '-Wl,--export-dynamic',
  '-DGUMMI_DATA=' + '"' + join_paths(meson.source_root(), 'data') + '"',
  '-DGUMMI_LIBS=' + '"' + join_paths(prefix, libdir, meson.project_name()) + '"',
  '-DGUMMI_LOCALES=' + '"' + join_paths(prefix, localesdir) + '"',
]

add_project_arguments(
  ['-DGETTEXT_PACKAGE=\\"@0@\\""'.format(meson.project_name())],
  language: 'c'
)

#add_project_arguments([
 #       '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name())
  #  ],
   # language: 'c',
#)

i18n = import('i18n')

conf_data = configuration_data()
conf_data.set('PACKAGE_NAME', 'Gummi')
conf_data.set('PACKAGE', meson.project_name())
conf_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf_data.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))

#install_data(['COPYING',], install_dir: join_paths(datadir,'licences/gummi/'))
install_data('COPYING', install_dir: join_paths(datadir, 'licenses', 'gummi'))

subdir('src')
subdir('data')
subdir('lib')
subdir('po')

meson.add_install_script('meson-post-install.py')
