# ADR 0001: CMake (+ presets, Ninja) as the build system

- Status: Accepted
- Date: 2026-11-23

## Context

The repo needs one build for the Godot-free core library, its tests, and the GDExtension.
godot-cpp supports both SCons and CMake. The Windows dev machine has MSVC 2022 Build Tools,
which bundle CMake and Ninja; SCons is not installed.

## Decision

Use CMake (minimum 3.25) with `CMakePresets.json` and the Ninja generator on all platforms.

- Presets: `debug` (core + tests + `template_debug` GDExtension), `release`
  (`template_release`), `core-only` (no godot-cpp; fastest loop for core work).
- godot-cpp is consumed with `add_subdirectory` and its official CMake support
  (`GODOTCPP_*` cache variables).
- `tools/build.ps1` enters the MSVC developer environment and runs a preset end to end.
- The GDExtension is written to `game/bin/` with godot-cpp's platform suffix so the
  `.gdextension` file can reference it directly.

## Consequences

- First-class IDE, `compile_commands.json` (clang-tidy), and CTest integration.
- Build flags differ slightly from Godot's SCons defaults; godot-cpp documents the differences.
- Contributors need CMake 3.25+ and Ninja (bundled with VS; `apt`/`brew` elsewhere).
