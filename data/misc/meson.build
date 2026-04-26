# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

app_id = 'app.silktex.SilkTex'

configure_file(
    input:         app_id + '.desktop.in',
    output:        app_id + '.desktop',
    install_dir:   appdir,
    install:       true,
    configuration: conf_data,
)

configure_file(
    input:         app_id + '.metainfo.xml.in',
    output:        app_id + '.metainfo.xml',
    install_dir:   join_paths(datadir, 'metainfo'),
    install:       true,
    configuration: conf_data,
)
