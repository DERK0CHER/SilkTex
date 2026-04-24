# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

blueprint_compiler = find_program('blueprint-compiler')

blueprints = custom_target(
    'blueprints',
    input:   files('main.blp'),
    output:  'main.ui',
    command: [blueprint_compiler, 'compile', '--output', '@OUTPUT@', '@INPUT@'],
)

silktex_resources = gnome.compile_resources(
    'silktex-resources',
    '../silktex.gresource.xml',
    c_name:       'silktex',
    dependencies: blueprints,
    source_dir:   meson.current_build_dir(),
)
