#!/usr/bin/env bash
#
# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a SilkTex Flatpak bundle from the working copy.
#
#   ./flatpak/build.sh                  # clean build + install locally
#   ./flatpak/build.sh --run            # same, plus launch the result
#   ./flatpak/build.sh --bundle         # export a distributable .flatpak file
#   ./flatpak/build.sh --offline        # build using only local runtimes/deps
#
# NOTE: flatpak-builder only runs on Linux. On macOS use the Nix dev
# shell (`./run.sh`) for regular development, and run this script on a
# Linux host / VM / CI to produce the Flatpak.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="$SCRIPT_DIR/app.silktex.SilkTex.yml"
BUILD_DIR="$REPO_DIR/build-flatpak"
STATE_DIR="$REPO_DIR/.flatpak-builder"
APP_ID="app.silktex.SilkTex"

usage() {
    sed -n '2,13p' "$0"
    exit "${1:-0}"
}

run_app=false
make_bundle=false
offline=false
for arg in "$@"; do
    case "$arg" in
        --run)    run_app=true ;;
        --bundle) make_bundle=true ;;
        --offline) offline=true ;;
        -h|--help) usage 0 ;;
        *) echo "unknown argument: $arg"; usage 1 ;;
    esac
done

if ! command -v flatpak >/dev/null || ! command -v flatpak-builder >/dev/null; then
    echo "flatpak and flatpak-builder are required (run this on Linux)." >&2
    exit 1
fi

if ! $offline; then
    # Install the runtime / SDK declared in the manifest.
    flatpak install --user --noninteractive flathub \
        org.gnome.Platform//50 \
        org.gnome.Sdk//50 \
        org.freedesktop.Sdk.Extension.texlive//25.08 \
        org.freedesktop.Sdk.Extension.rust-stable//25.08
else
    echo "Offline mode: skipping remote runtime/SDK installation."
fi

cd "$REPO_DIR"

build_args=(
    --force-clean
    --disable-rofiles-fuse
    --user
    --install
    --state-dir="$STATE_DIR"
)

if ! $offline; then
    build_args+=(--install-deps-from=flathub)
fi

flatpak-builder \
    "${build_args[@]}" \
    "$BUILD_DIR" \
    "$MANIFEST"

if $make_bundle; then
    BUNDLE_REPO="$REPO_DIR/build-flatpak-repo"
    BUNDLE_FILE="$REPO_DIR/${APP_ID}.flatpak"

    flatpak-builder \
        --user \
        --repo="$BUNDLE_REPO" \
        --state-dir="$STATE_DIR" \
        --force-clean \
        --disable-rofiles-fuse \
        "$BUILD_DIR" "$MANIFEST"

    flatpak build-bundle "$BUNDLE_REPO" "$BUNDLE_FILE" "$APP_ID" master
    echo "Wrote $BUNDLE_FILE"
fi

if $run_app; then
    flatpak run "$APP_ID"
fi
