# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later

silktex_lib_sources = files(
    'collab.c',
    'collab.h',
    'application.c',
    'application.h',
    'window.c',
    'window.h',
    'window-private.h',
    'window-git.c',
    'window-primary-menu.c',
    'editor.c',
    'editor.h',
    'compiler.c',
    'compiler.h',
    'git.c',
    'git.h',
    'preview.c',
    'preview.h',
    'prefs.c',
    'prefs.h',
    'cmdpalette.c',
    'cmdpalette.h',
    'searchbar.c',
    'searchbar.h',
    'snippets.c',
    'snippets.h',
    'synctex.c',
    'synctex.h',
    'structure.c',
    'structure.h',
    'latex.c',
    'latex.h',
    'configfile.c',
    'configfile.h',
    'style-schemes.c',
    'style-schemes.h',
    'constants.h',
    'utils.c',
    'utils.h',
)

silktex_lib = static_library(
    'silktex_core',
    silktex_lib_sources,
    dependencies: project_dependencies,
    c_args:       build_args,
)

# silktex_resources is linked directly into the executable (not the static lib)
# so macOS's dead-code stripper doesn't discard the GResource constructor.
executable(
    'silktex',
    'main.c',
    silktex_resources,
    link_with:    silktex_lib,
    dependencies: project_dependencies,
    c_args:       build_args,
    link_args:    build_link_args,
    install:      true,
)

# silktex-node: Rust P2P sidecar — built via cargo, placed next to the silktex binary.
# cargo handles its own incremental compilation, so build_always_stale is fine.
cargo = find_program('cargo', required: false)
if cargo.found()
    node_src        = meson.project_source_root() / 'silktex-node'
    node_target_dir = meson.current_build_dir() / 'silktex-node-target'

    # On macOS, cargo needs the SDK's lib dir (for libiconv etc.) unless the
    # environment already provides it (e.g. inside the Nix devshell).
    cargo_cmd = '''
set -e
if [ "$(uname)" = "Darwin" ]; then
  SDK_LIB="$(xcrun --show-sdk-path 2>/dev/null)/usr/lib"
  export LIBRARY_PATH="${SDK_LIB}${LIBRARY_PATH:+:$LIBRARY_PATH}"
fi
cargo build --release --manifest-path "$1/Cargo.toml" --target-dir "$2"
cp "$2/release/silktex-node" "$3"
'''

    custom_target(
        'silktex-node',
        output:            'silktex-node',
        command: [
            'sh', '-c', cargo_cmd,
            'sh', node_src, node_target_dir, '@OUTPUT@',
        ],
        build_by_default:  true,
        build_always_stale: true,
        console:           true,
        install:           true,
        install_dir:       get_option('bindir'),
    )
else
    warning('cargo not found — silktex-node will not be built; P2P collaboration disabled')
endif
