#!/usr/bin/env python3
"""Run Goblint on the Meson compile_commands.json (project-wide analysis).

Goblint expects a program entry point (`main`). Per-translation-unit runs on
files like application.c fail with "no suitable function to start from". The
documented approach is to pass the compilation database as a single input.

For GTK/Graphene on x86_64, CIL cannot parse GCC SIMD vector intrinsics. The
`__GI_SCANNER__` define makes Graphene use its scalar code paths (same idea as
gobject-introspection). The Goblint job sets `CFLAGS=-D__GI_SCANNER__` so Meson
records it in compile_commands.json; do not add it only in this script.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    compdb = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build" / "compile_commands.json"
    if not compdb.is_file():
        print(f"error: missing {compdb} (run: meson setup build)", file=sys.stderr)
        return 1

    goblint = shutil.which("goblint")
    if not goblint:
        print("error: goblint not on PATH", file=sys.stderr)
        return 1

    # Optional: skip when there are no C sources in the compile database.
    data = json.loads(compdb.read_text(encoding="utf-8"))
    if not any((e.get("file") or "").endswith(".c") for e in data):
        print("warning: no .c entries in compile_commands.json, skipping goblint", file=sys.stderr)
        return 0

    r = subprocess.run(
        [
            goblint,
            "--set",
            "pre.enabled",
            "false",
            # Limit context-sensitive call depth; helps large GTK call graphs.
            "--set",
            "ana.context.gas_value",
            "30",
            str(compdb),
        ],
        cwd=root,
    )
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
