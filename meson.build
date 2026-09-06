# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

project(
    'silktex',
    'c',
    version: '1.0.3',
    license: 'GPL-3.0-or-later',
    meson_version: '>=0.62.0',
    default_options: [
        'c_std=gnu11',
        'warning_level=2',
        'b_pie=true',
    ],
)

# Warning tuning. GObject/GIO idioms (short-form GActionEntry arrays,
# fixed-signature callbacks where some params are intentionally unused)
# trip these diagnostics constantly, so we disable them project-wide
# while keeping the rest of -Wextra active.
cc = meson.get_compiler('c')
add_project_arguments(
    cc.get_supported_arguments([
        '-Wno-unused-parameter',
        '-Wno-missing-field-initializers',
    ]),
    language: 'c',
)

i18n   = import('i18n')
gnome  = import('gnome')

prefix      = get_option('prefix')
bindir      = get_option('bindir')
datadir     = get_option('datadir')
libdir      = get_option('libdir')
localesdir  = get_option('localedir')

appdir      = join_paths(datadir, 'applications')
icondir     = join_paths(datadir, 'icons/hicolor/')
schemadir   = join_paths(datadir, 'glib-2.0/schemas')
mandir      = join_paths(datadir, 'man/man1/')

pkgdatadir  = join_paths(datadir, meson.project_name())

math_dep = cc.find_library('m', required: false)
intl_dep = dependency('intl', required: false)
if not intl_dep.found()
    intl_dep = cc.find_library('intl', required: false)
endif

project_dependencies = [
    dependency('gtk4'),
    dependency('libadwaita-1'),
    dependency('glib-2.0'),
    dependency('json-glib-1.0'),
    dependency('gtksourceview-5'),
    dependency('poppler-glib'),
    math_dep,
    intl_dep,
]

# Debug builds read data/ straight from the checkout so the app can run from
# the build tree; release/plain builds must use the installed pkgdatadir (the
# code falls back to XDG data dirs if this path does not exist).
if get_option('buildtype') in ['debug', 'debugoptimized']
    silktex_data_dir = join_paths(meson.project_source_root(), 'data')
else
    silktex_data_dir = join_paths(prefix, pkgdatadir)
endif

build_args = [
    '-DSILKTEX_DATA=' + '"' + silktex_data_dir + '"',
]

# The --export-dynamic linker flag is only meaningful on ELF targets.
build_link_args = []
if host_machine.system() != 'darwin'
    build_link_args += '-Wl,--export-dynamic'
endif

# Hardening. FORTIFY needs optimization and must not be redefined when the
# toolchain (e.g. the Nix cc wrapper) already sets it.
hardening_c_args = cc.get_supported_arguments(['-fstack-protector-strong'])
if get_option('optimization') in ['1', '2', '3', 's'] and cc.get_define('_FORTIFY_SOURCE') == ''
    hardening_c_args += '-D_FORTIFY_SOURCE=2'
endif
add_project_arguments(hardening_c_args, language: 'c')
add_project_link_arguments(
    cc.get_supported_link_arguments(['-Wl,-z,relro', '-Wl,-z,now']),
    language: 'c',
)

add_project_arguments(
    '-DGETTEXT_PACKAGE="' + meson.project_name() + '"',
    language: 'c',
)

conf_data = configuration_data()
conf_data.set('PACKAGE_NAME', 'SilkTex')
conf_data.set('PACKAGE', meson.project_name())
conf_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf_data.set_quoted('LOCALEDIR', join_paths(prefix, localesdir))
conf_data.set('DESKTOP_ICON', join_paths(pkgdatadir, 'icons', 'Silktex.svg'))

install_data(
    'COPYING',
    install_dir: join_paths(datadir, 'licenses', meson.project_name()),
)

subdir('data')
subdir('src')
subdir('tests')
subdir('po')

meson.add_install_script('meson-post-install.py')
