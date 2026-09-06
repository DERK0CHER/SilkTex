#!/usr/bin/env python3
# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Generate cargo-sources.json from silktex-node/Cargo.lock.

The Flathub manifest builds silktex-node with `cargo build --release
--offline`, so every locked crate has to be listed as a flatpak-builder
source.  The usual tool for that is flatpak-cargo-generator.py, but it
needs network access plus aiohttp and toml.  silktex-node has no git
dependencies and every crate in the lock carries its sha256, so the
whole manifest can be produced offline from Cargo.lock alone -- with
nothing but the standard library.

Usage:

    scripts/gen_cargo_sources.py            # rewrite cargo-sources.json
    scripts/gen_cargo_sources.py --check    # exit 1 if it is out of date

Re-run it after every change to silktex-node/Cargo.lock.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCKFILE = os.path.join(ROOT, "silktex-node", "Cargo.lock")
OUTFILE = os.path.join(ROOT, "cargo-sources.json")

CARGO_CONFIG = (
    "[source.vendored-sources]\n"
    'directory = "cargo/vendor"\n'
    "\n"
    "[source.crates-io]\n"
    'replace-with = "vendored-sources"\n'
)

_FIELD_RE = re.compile(r'^(name|version|checksum) = "(.*)"$')


def parse_lock(path: str) -> list[dict[str, str]]:
    """Return the [[package]] blocks of a Cargo.lock as dicts."""
    packages: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line == "[[package]]":
                current = {}
                packages.append(current)
                continue
            if current is None:
                continue
            match = _FIELD_RE.match(line)
            if match:
                current[match.group(1)] = match.group(2)
    return packages


def build_sources(packages: list[dict[str, str]]) -> list[dict[str, str]]:
    crates = [p for p in packages if p.get("checksum")]
    crates.sort(key=lambda p: (p["name"], p["version"]))

    sources: list[dict[str, str]] = []
    for crate in crates:
        name = crate["name"]
        version = crate["version"]
        checksum = crate["checksum"]
        dest = f"cargo/vendor/{name}-{version}"
        sources.append(
            {
                "type": "archive",
                "archive-type": "tar-gzip",
                "url": f"https://static.crates.io/crates/{name}/{name}-{version}.crate",
                "sha256": checksum,
                "dest": dest,
            }
        )
        sources.append(
            {
                "type": "inline",
                "contents": json.dumps({"package": checksum, "files": {}}),
                "dest": dest,
                "dest-filename": ".cargo-checksum.json",
            }
        )

    sources.append(
        {
            "type": "inline",
            "contents": CARGO_CONFIG,
            "dest": "cargo",
            "dest-filename": "config",
        }
    )
    return sources


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; exit non-zero if cargo-sources.json is stale",
    )
    args = parser.parse_args()

    packages = parse_lock(LOCKFILE)
    rendered = json.dumps(build_sources(packages), indent=4)

    if args.check:
        try:
            with open(OUTFILE, encoding="utf-8") as handle:
                existing = handle.read()
        except OSError as error:
            print(f"{OUTFILE}: {error}", file=sys.stderr)
            return 1
        if existing != rendered:
            print(
                f"{OUTFILE} is out of date; run scripts/gen_cargo_sources.py",
                file=sys.stderr,
            )
            return 1
        return 0

    with open(OUTFILE, "w", encoding="utf-8") as handle:
        handle.write(rendered)
    crate_count = sum(1 for p in packages if p.get("checksum"))
    print(f"wrote {OUTFILE}: {crate_count} crates, {len(rendered.splitlines())} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
