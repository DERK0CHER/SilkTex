#!/usr/bin/env bash
# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build (if needed) and launch SilkTex inside the Nix development shell.
#
# Usage:
#   ./run.sh              # incremental build + run
#   ./run.sh --clean      # wipe build dir, reconfigure, rebuild, run
#   ./run.sh --rebuild    # force a full reconfigure without wiping
#   ./run.sh --detach     # launch and return terminal immediately
#   ./run.sh -- <args>    # pass extra arguments through to silktex
#
# Any arguments after `--` are forwarded to the silktex binary.
# RUST_LOG defaults to silktex_node=info,iroh=error unless already set.

set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

BUILD_DIR="build-gtk4"
CLEAN=0
RECONFIGURE=0
DETACH=0
APP_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)    CLEAN=1; shift ;;
        --rebuild)  RECONFIGURE=1; shift ;;
        --detach)   DETACH=1; shift ;;
        -h|--help)
            sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --) shift; APP_ARGS=("$@"); break ;;
        *)  APP_ARGS+=("$1"); shift ;;
    esac
done

if ! command -v nix >/dev/null 2>&1; then
    echo "error: 'nix' is not on PATH. Install Nix or enter a shell that provides it." >&2
    exit 1
fi

pkill -f "$BUILD_DIR/src/silktex" 2>/dev/null || true
pkill -f "$BUILD_DIR/src/silktex-node" 2>/dev/null || true

if [[ $CLEAN -eq 1 ]]; then
    rm -rf "$BUILD_DIR"
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    nix develop --command meson setup "$BUILD_DIR"
elif [[ $RECONFIGURE -eq 1 ]]; then
    nix develop --command meson setup --reconfigure "$BUILD_DIR"
fi

nix develop --command ninja -C "$BUILD_DIR"

# Avoid Vulkan-specific issues/warnings on some Mesa/NixOS setups unless
# the user explicitly set a renderer.
GSK_RENDERER_VALUE="${GSK_RENDERER:-ngl}"
RUST_LOG_VALUE="${RUST_LOG:-silktex_node=info,iroh=error}"

if [[ $DETACH -eq 1 ]]; then
    echo ">> launching silktex (detached, GSK_RENDERER=$GSK_RENDERER_VALUE, RUST_LOG=$RUST_LOG_VALUE)"
    nix develop --command env GSK_RENDERER="$GSK_RENDERER_VALUE" RUST_LOG="$RUST_LOG_VALUE" \
        "$BUILD_DIR/src/silktex" "${APP_ARGS[@]}" >/dev/null 2>&1 &
    disown || true
    exit 0
fi

echo ">> launching silktex (foreground, GSK_RENDERER=$GSK_RENDERER_VALUE, RUST_LOG=$RUST_LOG_VALUE)"
echo ">> this keeps the terminal attached while the app is running (Ctrl+C to stop)"
exec nix develop --command env GSK_RENDERER="$GSK_RENDERER_VALUE" RUST_LOG="$RUST_LOG_VALUE" \
    "$BUILD_DIR/src/silktex" "${APP_ARGS[@]}"
