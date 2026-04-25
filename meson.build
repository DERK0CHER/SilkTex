# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

project(
    'silktex',
    'c',
    version: '1.0.2',
    license: 'GPL-3.0-or-later',
    meson_version: '>=0.62.0',
    default_options: [
        'c_std=gnu11',
        'warning_level=2',
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

# Directory the binary reads read-only data from at runtime. We keep the
# legacy `gummi` macro name (GUMMI_DATA) because it is referenced from
# several places, but the install path uses the project name.
pkgdatadir  = join_paths(datadir, meson.project_name())
# Alias kept for legacy subdirs/snippets/meson.build until they are
# modernised; safe to remove once those files no longer reference it.
gummidir    = pkgdatadir

# Primary target is Linux (Flatpak / distro packages). macOS is supported for
# local dev only. When pkg-config does not see GTK/Poppler (minimal shells,
# custom prefixes), extend the search path before failing.

fs = import('fs')
extra_pc_paths = []
_poppler_probe = dependency('poppler-glib', required: false)

if not _poppler_probe.found() and host_machine.system() == 'darwin'
    # Homebrew: metadata under $(brew --prefix)/lib/pkgconfig and often under
    # $(brew --prefix poppler)/lib/pkgconfig only.
    _brew = find_program('brew', required: false)
    if _brew.found()
        _r = run_command(_brew, '--prefix', capture: true, check: false)
        if _r.returncode() == 0
            _root = _r.stdout().strip()
            extra_pc_paths += join_paths(_root, 'lib', 'pkgconfig')
        endif
        _rp = run_command(_brew, '--prefix', 'poppler', capture: true, check: false)
        if _rp.returncode() == 0
            extra_pc_paths += join_paths(_rp.stdout().strip(), 'lib', 'pkgconfig')
        endif
    endif
elif not _poppler_probe.found() and host_machine.system() == 'linux'
    # Self-built stacks under /usr/local, multiarch libdirs, or --prefix=…
    # installs are common; Meson often runs without PKG_CONFIG_PATH set.
    _linux_pc_candidates = [
        join_paths(get_option('prefix'), 'lib', 'pkgconfig'),
        join_paths(
            get_option('prefix'),
            'lib',
            host_machine.cpu_family() + '-linux-gnu',
            'pkgconfig',
        ),
        '/usr/local/lib/pkgconfig',
        '/usr/local/lib/x86_64-linux-gnu/pkgconfig',
        '/usr/local/lib/aarch64-linux-gnu/pkgconfig',
        '/usr/lib/x86_64-linux-gnu/pkgconfig',
        '/usr/lib/aarch64-linux-gnu/pkgconfig',
    ]
    foreach _pc : _linux_pc_candidates
        if fs.is_dir(_pc)
            extra_pc_paths += _pc
        endif
    endforeach
endif

math_dep = cc.find_library('m', required: false)
if extra_pc_paths.length() > 0
    intl_dep = dependency('intl', required: false, pkg_config_path: extra_pc_paths)
else
    intl_dep = dependency('intl', required: false)
endif
if not intl_dep.found()
    intl_dep = cc.find_library('intl', required: false)
endif

if extra_pc_paths.length() > 0
    project_dependencies = [
        dependency('gtk4', pkg_config_path: extra_pc_paths),
        dependency('libadwaita-1', pkg_config_path: extra_pc_paths),
        dependency('glib-2.0', pkg_config_path: extra_pc_paths),
        dependency('json-glib-1.0', pkg_config_path: extra_pc_paths),
        dependency('gtksourceview-5', pkg_config_path: extra_pc_paths),
        dependency('poppler-glib', pkg_config_path: extra_pc_paths),
        math_dep,
        intl_dep,
    ]
else
    if _poppler_probe.found()
        _poppler_dep = _poppler_probe
    else
        _poppler_dep = dependency('poppler-glib', required: true)
    endif
    project_dependencies = [
        dependency('gtk4'),
        dependency('libadwaita-1'),
        dependency('glib-2.0'),
        dependency('json-glib-1.0'),
        dependency('gtksourceview-5'),
        _poppler_dep,
        math_dep,
        intl_dep,
    ]
endif

build_args = [
    '-DGUMMI_DATA='    + '"' + join_paths(meson.project_source_root(), 'data') + '"',
    '-DGUMMI_LIBS='    + '"' + join_paths(prefix, libdir, meson.project_name()) + '"',
    '-DGUMMI_LOCALES=' + '"' + join_paths(prefix, localesdir) + '"',
]

# The --export-dynamic linker flag is only meaningful on ELF targets.
if host_machine.system() != 'darwin'
    build_args += '-Wl,--export-dynamic'
endif

add_project_arguments(
    '-DGETTEXT_PACKAGE="' + meson.project_name() + '"',
    language: 'c',
)

conf_data = configuration_data()
conf_data.set('PACKAGE_NAME', 'SilkTex')
conf_data.set('PACKAGE', meson.project_name())
conf_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf_data.set_quoted('LOCALEDIR', join_paths(prefix, localesdir))

install_data(
    'COPYING',
    install_dir: join_paths(datadir, 'licenses', meson.project_name()),
)

subdir('data')
subdir('src')
subdir('po')

meson.add_install_script('meson-post-install.py')
