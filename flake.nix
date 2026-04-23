{
  description = "SilkTex Build Environment";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "aarch64-darwin"; # Apple Silicon
      pkgs = nixpkgs.legacyPackages.${system};
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
          texlive.bin.core
          gsettings-desktop-schemas
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

          # Also keep XDG_DATA_DIRS populated (used by other lookups, e.g. icons).
          export XDG_DATA_DIRS="${pkgs.gtk4}/share/gsettings-schemas/${pkgs.gtk4.name}:${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
        '';
      };
    };
}
