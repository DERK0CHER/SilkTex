# SilkTex - Modern LaTeX Editor
# Copyright (C) 2026 Bela Georg Barthelmes
# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "SilkTex Build Environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "aarch64-darwin"; # Apple Silicon
      pkgs = nixpkgs.legacyPackages.${system};

      # Combined TeX Live: use scheme-full to guarantee every CTAN
      # package is available.  This is large (~6 GB after substitution)
      # but eliminates "File foo.sty not found" surprises across the
      # many packages real-world LaTeX documents load (bbold, enumitem,
      # mathtools, tikz, biblatex, fontawesome5, …).
      texliveEnv = pkgs.texlive.combined.scheme-full;
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        # Native tools used at build/runtime inside the shell
        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
          clang
          blueprint-compiler
        ];

        # Libraries resolved by pkg-config
        buildInputs = with pkgs; [
          gtk4
          libadwaita
          glib
          gtksourceview5
          gtkspell3
          poppler
          intltool
          texliveEnv
          gsettings-desktop-schemas
          # Symbolic icons used by the header bar, sidebar, preview
          # toolbar, etc. (document-new-symbolic, sidebar-show-symbolic,
          # format-text-bold-symbolic, zoom-fit-best-symbolic,
          # view-dual-symbolic, …). Without this the buttons fall back to
          # an empty / generic glyph.
          adwaita-icon-theme
          hicolor-icon-theme
        ];

        shellHook = ''
          echo "SilkTex dev shell loaded"
          echo "GTK4: $(pkg-config --modversion gtk4 2>/dev/null || echo missing)"
          echo "Adwaita: $(pkg-config --modversion libadwaita-1 2>/dev/null || echo missing)"

          # GLib checks GSETTINGS_SCHEMA_DIR first (colon-separated list of
          # directories containing gschemas.compiled), so set it explicitly.
          # This is required for GTK's file chooser which loads
          # org.gtk.gtk4.Settings.FileChooser at runtime.
          export GSETTINGS_SCHEMA_DIR="${pkgs.gtk4}/share/gsettings-schemas/${pkgs.gtk4.name}/glib-2.0/schemas:${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}/glib-2.0/schemas''${GSETTINGS_SCHEMA_DIR:+:$GSETTINGS_SCHEMA_DIR}"

          # XDG_DATA_DIRS is scanned for share/icons/<theme>/… by GTK's
          # icon theme loader. We include Adwaita + Hicolor so symbolic
          # icons resolve, and keep the gsettings-schemas dirs so that
          # file-chooser settings keep loading.
          export XDG_DATA_DIRS="${pkgs.adwaita-icon-theme}/share:${pkgs.hicolor-icon-theme}/share:${pkgs.gtk4}/share/gsettings-schemas/${pkgs.gtk4.name}:${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

          # Ensure GTK picks the bundled icon theme by default.
          export GTK_ICON_THEME="Adwaita"
        '';
      };
    };
}
