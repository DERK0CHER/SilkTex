#!/usr/bin/env python3
"""Preprocess each src/*.c from compile_commands.json, then run Goblint on the .i files."""

from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


def is_app_src(path: str) -> bool:
    p = path.replace("\\", "/")
    return "/src/" in p and p.endswith(".c") and "silktex-resources" not in p


def command_to_preprocess_argv(cmd: str, build_dir: Path, i_out: Path) -> list[str] | None:
    parts = shlex.split(cmd, posix=True)
    if not parts:
        return None
    compiler = parts[0]
    if Path(compiler).name not in {"clang", "clang++", "gcc", "cc"}:
        compiler = "clang"
    args = parts[1:]
    src: str | None = None
    filtered: list[str] = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-c":
            i += 1
            continue
        if a in ("-o", "-MF", "-MQ", "-MT"):
            i += 2
            continue
        if a.startswith("-o") and len(a) > 2:
            i += 1
            continue
        if a == "-MD" or a == "-MMD":
            i += 1
            continue
        if re.match(r"^-M[FTQ]", a):
            i += 2
            continue
        if a.endswith(".c") and not a.startswith("-"):
            src = a
            i += 1
            continue
        filtered.append(a)
        i += 1
    if not src:
        return None
    return [compiler, "-E", *filtered, src, "-o", str(i_out)]


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    compdb = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build" / "compile_commands.json"
    pre_dir = (Path(sys.argv[2]) if len(sys.argv) > 2 else root / ".goblint" / "preprocessed").resolve()
    pre_dir.mkdir(parents=True, exist_ok=True)

    if not compdb.is_file():
        print(f"error: missing {compdb} (run: meson setup build)", file=sys.stderr)
        return 1

    goblint = shutil.which("goblint")
    if not goblint:
        print("error: goblint not on PATH", file=sys.stderr)
        return 1

    data = json.loads(compdb.read_text(encoding="utf-8"))
    for e in data:
        f = e.get("file", "")
        if not is_app_src(f):
            continue
        cmd = e.get("command")
        if not cmd:
            continue
        build_dir = Path(e.get("directory", root))
        base = Path(f).name
        i_path = (pre_dir / f"{base}.i").resolve()
        argv = command_to_preprocess_argv(cmd, build_dir, i_path)
        if not argv:
            print(f"skip (could not build preprocess argv): {f}", file=sys.stderr)
            continue
        r = subprocess.run(argv, cwd=build_dir)
        if r.returncode != 0:
            print(f"error: preprocess failed: {f}", file=sys.stderr)
            return r.returncode
        print(f"== goblint {f}", flush=True)
        r = subprocess.run(
            [goblint, "--set", "pre.enabled", "false", str(i_path)],
            cwd=root,
        )
        if r.returncode != 0:
            return r.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
