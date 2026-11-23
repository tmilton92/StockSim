# ADR 0003: Third-party C++ dependencies as pinned git submodules

- Status: Accepted
- Date: 2026-11-23

## Context

The build depends on godot-cpp and Catch2. Versions must be exact and reproducible, and the
build should work offline after cloning.

## Decision

- Third-party sources live under `/third_party/` as git submodules pinned to release tags:
  - `third_party/godot-cpp` at `10.0.0-stable`
  - `third_party/Catch2` at `v3.16.1`
- `/third_party` is an addition to the layout in CLAUDE.md; nothing in it is edited locally.
- godot-cpp 10+ is versioned independently of Godot and has no `4.7` branch; it targets a
  Godot API through an API description file. We build it against `gdext/extension_api.json`,
  dumped from the pinned engine binary (`godot --headless --dump-extension-api`), rather than
  the generic 4.7.0 file shipped inside godot-cpp, so the bindings match the exact engine build
  recorded in `docs/VERSIONS.md`.
- Upgrading a dependency means: bump the submodule, update `docs/VERSIONS.md`, full test pass.
  The engine and godot-cpp are upgraded together (CLAUDE.md).

## Consequences

- Clone with `git clone --recurse-submodules` (or `git submodule update --init --recursive`).
- `extension_api.json` (about 7 MB) is checked in; regenerate it on every engine upgrade.
