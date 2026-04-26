# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

app_id = 'app.silktex.SilkTex'

install_data(
    'Silktex.svg',
    install_dir: join_paths(icondir, 'scalable/apps/'),
    rename: app_id + '.svg',
)

# A copy inside the app's private data dir so the code can find it even
# when no icon theme is available (e.g. inside the Flatpak sandbox).
install_data('Silktex.svg', install_dir: join_paths(pkgdatadir, 'icons'))
