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
        ];

        shellHook = ''
          echo "SilkTex dev shell loaded"
          echo "GTK4: $(pkg-config --modversion gtk4 2>/dev/null || echo missing)"
          echo "Adwaita: $(pkg-config --modversion libadwaita-1 2>/dev/null || echo missing)"
        '';
      };
    };
}
