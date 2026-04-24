#!/usr/bin/env python3
# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Post-install actions run by meson.

We skip everything when DESTDIR is set so that distribution packagers
(Flatpak, Nix, .deb, …) remain in control of icon-cache / schema
regeneration.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

if "DESTDIR" in os.environ:
    sys.exit(0)

prefix = os.environ.get("MESON_INSTALL_PREFIX", "/usr")
datadir = os.path.join(prefix, "share")


def _run(cmd: list[str], description: str) -> None:
    if shutil.which(cmd[0]) is None:
        print(f"skipping: {description} ({cmd[0]} not on PATH)")
        return
    print(f"{description} …")
    subprocess.call(cmd)


_run(
    ["glib-compile-schemas", os.path.join(datadir, "glib-2.0", "schemas")],
    "Compiling GSettings schemas",
)
_run(
    ["gtk-update-icon-cache", "-qtf", os.path.join(datadir, "icons", "hicolor")],
    "Updating icon cache",
)
_run(
    ["update-desktop-database", "-q", os.path.join(datadir, "applications")],
    "Updating desktop database",
)
