#!/usr/bin/env bash
# Run Goblint on each src/*.c (via preprocess → .i). See scripts/goblint_all.py.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$(uname -s)" == "Darwin" ]]; then
  echo "warning: on macOS, Goblint may still fail on preprocessed libc headers." >&2
  echo "  Prefer Linux or GitHub Actions for a clean full-project run." >&2
fi
exec python3 "$ROOT/scripts/goblint_all.py" "${1:-$ROOT/build/compile_commands.json}" "${2:-$ROOT/.goblint/preprocessed}"
