#!/usr/bin/env bash
# Run Goblint on the Meson compile database (see scripts/goblint_all.py).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$(uname -s)" == "Darwin" ]]; then
  echo "warning: on macOS, Goblint may still fail on preprocessed libc headers." >&2
  echo "  Prefer Linux or GitHub Actions for a clean project-wide run." >&2
fi
# Optional: export CFLAGS='-D__GI_SCANNER__' when configuring Meson (Graphene + CIL).
# Run ninja -C build once so generated sources (e.g. silktex-resources.c) exist.
exec python3 "$ROOT/scripts/goblint_all.py" "${1:-$ROOT/build/compile_commands.json}"
