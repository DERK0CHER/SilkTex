#!/usr/bin/env python3

# Package constants 
C_PACKAGE_NAME = "Gummi"
C_PACKAGE_VERSION = "0.8.3"
C_PACKAGE_URL = "https://github.com/alexandervdm/gummi"
C_PACKAGE_COPYRIGHT = "Copyright © 2009-2025 Gummi Developers"
C_PACKAGE_LICENSE = "MIT License"
C_PACKAGE_COMMENTS = "Simple LaTeX Editor"
C_PACKAGE_GUIDE = "https://github.com/alexandervdm/gummi/wiki"

# Directory paths
C_TMPDIR = "/tmp/gummi"  # Should be configurable 
C_GUMMI_CONFDIR = "~/.config/gummi"
C_GUMMI_TEMPLATEDIR = "~/.config/gummi/templates"

# File permissions
DIR_PERMS = 0o755

# Command constants
C_LATEX = "latex"
C_PDFLATEX = "pdflatex" 
C_XELATEX = "xelatex"
C_LUALATEX = "lualatex"
C_RUBBER = "rubber"
C_LATEXMK = "latexmk"
C_TEXSEC = "firejail"  # Security wrapper

# Editor options
STR_EQU = str.__eq__  # String comparison helper
