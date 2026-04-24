# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

app_id = 'app.silktex.SilkTex'

install_data(
    'icon.png',
    install_dir: join_paths(icondir, '256x256/apps/'),
    rename: app_id + '.png',
)

install_data(
    'icon.png',
    install_dir: join_paths(icondir, '512x512/apps/'),
    rename: app_id + '.png',
)

# A copy inside the app's private data dir so the code can find it even
# when no icon theme is available (e.g. inside the Flatpak sandbox).
install_data('icon.png', install_dir: join_paths(pkgdatadir, 'icons'))
