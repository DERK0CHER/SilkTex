# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:

- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:

- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:

- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:

- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## Project

SilkTex is a GTK 4 / libadwaita LaTeX editor written in C (GNU11). It uses GtkSourceView for editing, Poppler for PDF preview, and Blueprint for UI definitions. Build system is Meson; the recommended dev environment is Nix via `flake.nix`.

## Build & Run

```bash
./run.sh              # incremental build + launch
./run.sh --clean      # wipe build dir, reconfigure, rebuild, launch
./run.sh --rebuild    # force reconfigure (needed after meson.build changes)
./run.sh -- file.tex  # pass args to the silktex binary
```

Without Nix (Linux with GNOME stack installed):

```bash
meson setup build-gtk4
ninja -C build-gtk4
./build-gtk4/src/silktex
```

## Code Style

Format C with:

```bash
clang-format -i src/*.c src/*.h
```

The project uses GObject idioms throughout (class macros, signals, `G_DEFINE_TYPE`). `-Wno-missing-field-initializers` and `-Wno-unused-parameter` are intentionally silenced to accommodate GObject patterns.

## Architecture

```
main.c
  └─ SilktexApplication (application.c)  — GApplication subclass; config init, app.* actions
       └─ SilktexWindow (window.c)        — AdwApplicationWindow; tab management, win.* actions
            ├─ SilktexEditor (editor.c)   — GtkSourceView wrapper; file I/O, text ops, styling
            ├─ SilktexCompiler (compiler.c)— Async pdflatex/bibtex/makeindex; runs on a worker thread, signals back to main thread
            ├─ SilktexPreview (preview.c) — Poppler PDF viewer; HiDPI-aware, SyncTeX click targets
            └─ supporting modules:
                 snippets.c   — VS Code-style snippet engine with $1…$0 tab stops and $FILENAME/$BASENAME/$SELECTED_TEXT macros
                 synctex.c    — Forward (editor→PDF) and inverse (PDF→editor) sync
                 structure.c  — Document outline tree view
                 searchbar.c  — Find/replace
                 prefs.c      — Preferences dialog (font, spell-check, snippets, keybindings)
                 configfile.c — JSON config persistence
                 git.c        — Git integration (status, commit, clone)
                 latex.c      — Insert-menu helpers (image, table, matrix, bibliography)
                 collab.c     — P2P collaboration glue; spawns and talks to silktex-node
```

### `silktex-node/` subproject (P2P collaboration)

A separate Rust binary that powers the real-time collaboration feature. Built by Cargo as part of the Meson build and shipped alongside `silktex`. The C side (`collab.c`) launches it as a child process and communicates with it; the node handles the network and CRDT layers.

Stack:
- **Loro** — CRDT for conflict-free concurrent text edits.
- **iroh** — direct peer-to-peer transport (QUIC, with DERP relay fallback).
- **tokio** — async runtime.

Layout: `silktex-node/src/main.rs` (entry), `net.rs` (iroh transport), `doc.rs` (Loro document state).

**Threading:** GTK/UI runs on the main thread. The compiler launches a worker thread for `pdflatex`; results are marshalled back via GLib signals.

**UI definitions** live in `data/ui/` as Blueprint (`.blp`) files, compiled to GResource at build time via `data/silktex.gresource.xml`.

**Snippets** are JSON files in `data/snippets/` and are loaded at startup by `snippets.c`.

## Key Files

| File               | Role                                                      |
| ------------------ | --------------------------------------------------------- |
| `src/window.c`     | Central coordinator — tabs, file dialogs, action handlers |
| `src/compiler.c`   | Async LaTeX build pipeline                                |
| `src/preview.c`    | Poppler rendering, zoom, SyncTeX                          |
| `src/editor.c`     | GtkSourceView abstraction                                 |
| `src/prefs.c`      | All user-configurable settings                            |
| `data/ui/main.blp` | Main window UI layout (Blueprint)                         |
| `meson.build`      | Build configuration and dependency declarations           |
| `flake.nix`        | Pinned Nix dev environment                                |
