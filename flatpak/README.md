# SilkTex Flatpak

This directory contains everything needed to build a Flatpak of SilkTex.

## Files

| File                                 | Purpose                                      |
| ------------------------------------ | -------------------------------------------- |
| `app.silktex.SilkTex.yml`            | Flatpak-builder manifest (app-id, modules)   |
| `build.sh`                           | Helper script that runs flatpak-builder      |

The corresponding desktop entry, metainfo, and app icons live in
`../data/misc/` and `../data/icons/` and are installed automatically by
Meson during the Flatpak build.

## Requirements

Flatpak can only be built on Linux. On macOS (where this repo is being
developed) you can either:

- Copy the repository to a Linux machine / VM and run `./flatpak/build.sh`.
- Run the same script inside a Docker container based on `fedora:latest`
  with `flatpak` and `flatpak-builder` installed.

## Building

```bash
# First time on a new machine:
flatpak install --user flathub org.gnome.Platform//49 org.gnome.Sdk//49

# Clean build and install into the user's Flatpak repository:
./flatpak/build.sh

# Launch it:
flatpak run app.silktex.SilkTex
# or:
./flatpak/build.sh --run

# Produce a distributable .flatpak bundle next to the repo:
./flatpak/build.sh --bundle
```

## Notes

- The GNOME 49 runtime already includes `gtk4`, `libadwaita`,
  `gtksourceview5`, and `poppler-glib` — SilkTex is the only module the
  manifest has to build.
- `synctex` is made optional in `meson.build`. SilkTex invokes the
  `synctex` CLI at runtime, which the host's TeX Live provides; forward
  / inverse sync therefore keeps working because of the `--filesystem=host`
  permission declared in the manifest.
- LaTeX itself (`pdflatex`, `bibtex`, `makeindex`) is also used from the
  host system via `--talk-name=org.freedesktop.Flatpak` and
  `--filesystem=host`. Bundling TeX Live would add several GB to the
  Flatpak and is not done here.
