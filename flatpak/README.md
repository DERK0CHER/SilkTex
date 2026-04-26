# SilkTex Flatpak

This directory contains everything needed to build SilkTex as a Flatpak,
and the pieces that are submitted to [Flathub](https://flathub.org).

## Files

| File                                  | Purpose                                                |
| ------------------------------------- | ------------------------------------------------------ |
| `app.silktex.SilkTex.yml`             | Local Flatpak-builder manifest for GNOME Builder       |
| `app.silktex.SilkTex.flathub.yml`     | Release manifest to copy into the Flathub repository   |
| `flathub.json`                        | Flathub build options (architectures, auto-merge, etc.) |
| `build.sh`                            | Helper script that runs `flatpak-builder` locally      |

The desktop entry (`app.silktex.SilkTex.desktop.in`), AppStream metainfo
(`app.silktex.SilkTex.metainfo.xml.in`) and app icon live in
`../data/misc/` and `../data/icons/` and are installed automatically by
Meson during the Flatpak build.

The GNOME 50 runtime supplies the GTK/libadwaita stack. Poppler's GLib
bindings are built by the manifest because they are not part of the SDK.

## Building locally (development)

Flatpak-builder only runs on Linux.  On macOS use the Nix dev shell
(`./run.sh`) for regular development and only reach for Flatpak when you
want to reproduce what Flathub will build, or to test end-user packaging.

```bash
# One-time: install the runtime + SDK declared by the manifest.
flatpak install --user flathub org.gnome.Platform//50 org.gnome.Sdk//50

# Clean build and install into the user's Flatpak repository:
./flatpak/build.sh

# Launch the installed Flatpak:
flatpak run app.silktex.SilkTex
# or:
./flatpak/build.sh --run

# Produce a distributable .flatpak bundle at ./app.silktex.SilkTex.flatpak :
./flatpak/build.sh --bundle
```

GNOME Builder can use `app.silktex.SilkTex.yml` directly. It points at
`path: ..`, so Builder and `flatpak-builder` both build your working copy
instead of downloading the tagged release from GitHub.

## Submitting to Flathub (one-time)

Flathub is a curated store. The app has to be accepted once; from that
moment on, the Flathub repo `github.com/flathub/app.silktex.SilkTex` is
the build source, and updates become automatic (see the next section).

1. Fork <https://github.com/flathub/flathub>.
2. Create a branch named **`new-pr`** (the name is required by the bot).
3. Copy the following files from this repo into the root of your fork:
   - `flatpak/app.silktex.SilkTex.flathub.yml` → `app.silktex.SilkTex.yml`
   - `flatpak/flathub.json`            → `flathub.json`
4. Open a PR against `flathub/flathub:new-pr`. `flathubbot` will build
   it automatically and report back. A human reviewer then audits the
   manifest, finish-args, metainfo, icons, etc.
5. Once merged, a new repo is created at
   `github.com/flathub/app.silktex.SilkTex` and you become its
   maintainer. Every push to its `master` branch triggers a Flathub
   Buildbot build that publishes to the `stable` channel on merge.

### Things to double-check before submitting

- **Screenshots.** `data/misc/app.silktex.SilkTex.metainfo.xml.in` has
  placeholder screenshot URLs pointing at `github.com/DERK0CHER/SilkTex`
  on an `assets` branch. Push real PNGs (recommended ~1280x720) there,
  or rewrite the URLs to wherever you host them. Flathub will reject
  apps without at least one working screenshot.
- **Validate the metainfo.** Inside the dev shell:
  ```bash
  nix develop --command appstreamcli validate --pedantic \
      data/misc/app.silktex.SilkTex.metainfo.xml.in
  ```
- **Reproducible sources.** The Flathub manifest uses `type: git` with
  `tag: v1.0.2`. Before submitting, tag a real release in this repo and
  add a matching `commit:` SHA:
  ```bash
  git tag -a v1.0.2 -m "SilkTex 1.0.2"
  git push origin v1.0.2
  sha=$(git rev-parse v1.0.2^{commit})
  python3 - <<PY
  from pathlib import Path
  p = Path("flatpak/app.silktex.SilkTex.flathub.yml")
  s = p.read_text()
  s = s.replace("        tag: v1.0.2\n", "        tag: v1.0.2\n        commit: $sha\n")
  p.write_text(s)
  PY
  git add flatpak/app.silktex.SilkTex.flathub.yml
  git commit -m "flatpak: pin release source"
  git push
  ```
- **Sandbox review.** The manifest keeps `--filesystem=host` and
  `--talk-name=org.freedesktop.Flatpak` because SilkTex shells out to
  the host's `pdflatex` / `bibtex` / `makeindex` / `synctex`. This is
  the same model that TeXstudio and similar LaTeX editors use on
  Flathub, but reviewers may still push back. Fallback options are
  documented in the header of `app.silktex.SilkTex.yml`.

## Automated updates after acceptance

Once SilkTex is on Flathub there are two independent automation paths
keeping its Flathub manifest in sync with this repo:

### 1. `flatpak-external-data-checker` (Flathub-side, zero config)

The manifest's source block is annotated:

```yaml
x-checker-data:
  type: git
  tag-pattern: '^v([\d.]+)$'
```

Flathub runs `flatpak-external-data-checker` on a schedule against every
app repo. With that annotation it scans this repo's git tags, picks the
newest one matching `v1.2.3`, rewrites `tag:` + `commit:` accordingly
and opens a PR against the Flathub repo. Because `flathub.json` has
`"automerge-flathubbot-prs": true`, the PR can auto-merge once CI goes
green, which then triggers a new Flathub build.

### 2. Our own `release: published` workflow (this repo)

`.github/workflows/flathub-release.yml` fires on every published GitHub
release, resolves the tag to a commit SHA, clones
`flathub/app.silktex.SilkTex`, rewrites the manifest and opens a bump PR.
It needs a repository secret named `FLATHUB_BUMP_TOKEN` — a GitHub
Personal Access Token scoped to the Flathub repo with `contents: write`
and `pull-requests: write`.

This is belt-and-braces: FEDC already does the same job on Flathub's
side, but running it from here reacts within seconds of a release
instead of on FEDC's schedule. You can use either, both, or neither —
they do not conflict (PRs are idempotent).

### Release checklist

```bash
# 1. Bump the version in meson.build and data/misc/...metainfo.xml.in
#    (add a new <release version="X.Y.Z"> block).
# 2. Tag + push.
git tag -a vX.Y.Z -m "SilkTex X.Y.Z"
git push origin vX.Y.Z

# 3. Create a GitHub release for that tag (or `gh release create vX.Y.Z`).
#    The `Update Flathub manifest on release` workflow runs automatically
#    and opens a bump PR on flathub/app.silktex.SilkTex.

# 4. Merge the Flathub PR. Flathub Buildbot builds and publishes the
#    update to the `stable` channel within ~30 minutes.
```

## Notes on bundled vs host TeX Live

LaTeX itself (`pdflatex`, `bibtex`, `makeindex`, `synctex`) is used from
the host — install `texlive-full` / `texlive-scheme-full` on the host
system. The manifest requests `--filesystem=host` and the
`org.freedesktop.Flatpak` talk-name so SilkTex can run those tools through
`flatpak-spawn --host`. Bundling TeX Live would add several GB to every
Flatpak download and is avoided deliberately.

If Flathub asks for a tighter sandbox, the alternative is to build
against the `org.freedesktop.Sdk.Extension.texlive` SDK extension, drop
the host-spawn permissions, and adjust `src/compiler.c` to look in
`/app/bin/` first. The manifest header comments outline that migration.
