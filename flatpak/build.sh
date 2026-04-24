#!/usr/bin/env bash
#
# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a SilkTex Flatpak bundle from the working copy.
#
#   ./flatpak/build.sh            # clean build + install into ./build-flatpak
#   ./flatpak/build.sh --run      # same, plus launch the result
#   ./flatpak/build.sh --bundle   # export a distributable .flatpak file
#
# The committed manifest (app.silktex.SilkTex.yml) uses `type: git` so
# Flathub consumes it as-is. For local builds we generate a scratch
# manifest that swaps the source for `type: dir`, so you build whatever
# is on disk instead of what is pushed to GitHub.
#
# NOTE: flatpak-builder only runs on Linux. On macOS use the Nix dev
# shell (`./run.sh`) for regular development, and run this script on a
# Linux host / VM / CI to produce the Flatpak.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_MANIFEST="$SCRIPT_DIR/app.silktex.SilkTex.yml"
LOCAL_MANIFEST="$SCRIPT_DIR/.local.app.silktex.SilkTex.yml"
BUILD_DIR="$REPO_DIR/build-flatpak"
STATE_DIR="$REPO_DIR/.flatpak-builder"
APP_ID="app.silktex.SilkTex"

usage() {
    sed -n '2,16p' "$0"
    exit "${1:-0}"
}

run_app=false
make_bundle=false
for arg in "$@"; do
    case "$arg" in
        --run)    run_app=true ;;
        --bundle) make_bundle=true ;;
        -h|--help) usage 0 ;;
        *) echo "unknown argument: $arg"; usage 1 ;;
    esac
done

if ! command -v flatpak >/dev/null || ! command -v flatpak-builder >/dev/null; then
    echo "flatpak and flatpak-builder are required (run this on Linux)." >&2
    exit 1
fi

# Derive a local manifest that points at the working copy instead of the
# remote git repo.  Strip the `- type: git` … block (and its
# x-checker-data annotation) and replace it with a `type: dir` source.
python3 - "$SRC_MANIFEST" "$LOCAL_MANIFEST" <<'PY'
import re, sys, pathlib
# The `sources:` block is the last field of the last module in the
# manifest, so we can just replace everything from that line to EOF
# with a minimal `type: dir` source pointing at the repo root.
src = pathlib.Path(sys.argv[1]).read_text()
out = re.sub(
    r'(?ms)^    sources:\n.*\Z',
    '    sources:\n      - type: dir\n        path: ..\n',
    src,
)
pathlib.Path(sys.argv[2]).write_text(out)
PY
trap 'rm -f "$LOCAL_MANIFEST"' EXIT

# Install the runtime / SDK declared in the manifest.
flatpak install --user --noninteractive flathub \
    org.gnome.Platform//49 \
    org.gnome.Sdk//49

cd "$REPO_DIR"

flatpak-builder \
    --force-clean \
    --user \
    --install \
    --install-deps-from=flathub \
    --state-dir="$STATE_DIR" \
    "$BUILD_DIR" \
    "$LOCAL_MANIFEST"

if $make_bundle; then
    BUNDLE_REPO="$REPO_DIR/build-flatpak-repo"
    BUNDLE_FILE="$REPO_DIR/${APP_ID}.flatpak"

    flatpak-builder \
        --user \
        --repo="$BUNDLE_REPO" \
        --state-dir="$STATE_DIR" \
        --force-clean \
        "$BUILD_DIR" "$LOCAL_MANIFEST"

    flatpak build-bundle "$BUNDLE_REPO" "$BUNDLE_FILE" "$APP_ID" master
    echo "Wrote $BUNDLE_FILE"
fi

if $run_app; then
    flatpak run "$APP_ID"
fi
