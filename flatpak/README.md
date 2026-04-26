# SilkTex Flatpak

This directory contains everything needed to build SilkTex as a Flatpak,
and the pieces that are submitted to [Flathub](https://flathub.org).

## Files

| File                                  | Purpose                                                |
| ------------------------------------- | ------------------------------------------------------ |
| `app.silktex.SilkTex.yml`             | Local Flatpak-builder manifest for GNOME Builder       |
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
# One-time: install the runtime, SDK and TeX Live extension.
flatpak install --user flathub \
    org.gnome.Platform//50 \
    org.gnome.Sdk//50 \
    org.freedesktop.Sdk.Extension.texlive//25.08

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

### GNOME Builder: `fusermount3` / `rofiles-fuse` errors

`flatpak-builder` uses **rofiles-fuse** under its state directory. If a
build is interrupted, mounts can be left behind; the next run may fail
with errors like `fusermount3: ... Permission denied` or `Failure
spawning rofiles-fuse`.

1. Quit GNOME Builder.
2. Unmount stale rofiles mounts, then remove the cache (adjust the path
   if your projects live elsewhere), for example:
   ```bash
   STATE="$HOME/Projects/.gnome-builder/flatpak-builder"
   findmnt -T "$STATE/rofiles" 2>/dev/null || true
   for d in "$STATE"/rofiles/rofiles-*; do
     [ -d "$d" ] || continue
     fusermount3 -u "$d" 2>/dev/null || umount "$d" 2>/dev/null || true
   done
   rm -rf "$STATE/rofiles"
   ```
3. Build again from Builder.

As a fallback, build from a terminal with this repo’s helper script; it
passes `--disable-rofiles-fuse` to `flatpak-builder` (builds are a bit
slower but avoid rofiles-fuse entirely):

```bash
./flatpak/build.sh
```

## Submitting to Flathub (one-time)

Flathub is a curated store. The app has to be accepted once; from that
moment on, the Flathub repo `github.com/flathub/app.silktex.SilkTex` is
the build source, and updates become automatic (see the next section).

1. Fork <https://github.com/flathub/flathub>.
2. Create a branch named **`new-pr`** (the name is required by the bot).
3. Copy the following root-level files from this repo into the root of your fork:
   - `app.silktex.SilkTex.yml`
   - `flathub.json`
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
- **Reproducible sources.** The Flathub manifest uses `type: git` sources
  with both `tag:` and `commit:` pinned. Update both when cutting a new
  release.
- **Sandbox review.** The manifest keeps `--filesystem=host` and
  uses `org.freedesktop.Sdk.Extension.texlive` for
  `pdflatex` / `bibtex` / `makeindex` / `synctex`, so LaTeX compilation
  works without host-spawn permissions.

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

## Notes on TeX Live

The root-level Flathub manifest uses
`org.freedesktop.Sdk.Extension.texlive`, mounted at `/app/texlive`, so the
Flatpak can run `pdflatex`, `bibtex`, `makeindex`, and `synctex` without
host-spawn permissions.

The local development manifest in this directory is still optimized for
GNOME Builder and local testing of the current checkout.
