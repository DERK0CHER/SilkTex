# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

subdir('icons')
subdir('misc')
subdir('snippets')
subdir('ui')

install_data(
    'templates/vorlage.tex',
    install_dir: join_paths(pkgdatadir, 'templates'),
)

install_data(
    files(
        'styles/silktex-gruvbox-dark.xml',
        'styles/silktex-gruvbox-light.xml',
        'styles/silktex-lights-out.xml',
    ),
    install_dir: join_paths(pkgdatadir, 'styles'),
)
