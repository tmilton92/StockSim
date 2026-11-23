# Pinned versions

Upgrade the engine and godot-cpp together, never one alone (see CLAUDE.md and ADR 0003).

| Component | Version | Notes |
| --------- | ------- | ----- |
| Godot engine | **4.7.2-stable** (`v4.7.2.stable.official.ed1daf0bf`) | Linux zip SHA-512 pinned in `.github/workflows/ci.yml` |
| godot-cpp | `10.0.0-stable` (`507ed9d840c01a3c5b2a39af8bb4000bfac30bf5`) | Built against `gdext/extension_api.json` dumped from the engine above |
| Catch2 | `v3.16.1` (`08092139210881f01a34e60c5f2fd6afa0ea4024`) | |
| C++ standard | C++20 | godot-cpp itself builds as C++17 |
| CMake | 3.25 minimum | Dev machine: 3.31.6 (bundled with VS 2022 Build Tools) |
| MSVC | 14.44 (VS 2022 Build Tools) | |
| clang-format / clang-tidy | 19.1.5 | MSVC-bundled LLVM locally; pip (format) / apt 19.x (tidy) in CI |

## Regenerating `extension_api.json`

```powershell
Set-Location gdext
& "<path>\Godot_v4.7.2-stable_win64_console.exe" --headless --dump-extension-api
```

## Engine notes, regressions and workarounds

- **4.7: `debug/gdscript/warnings/exclude_addons` no longer exists.** It was replaced by
  `debug/gdscript/warnings/directory_rules` (dictionary of path to Exclude/Include), which
  defaults to `{ "res://addons": Exclude }`. Verified against the 4.7.2 binary's
  `ProjectSettings` property list.
- **clang-tidy with an MSVC compile database:** the UCRT `offsetof` macro is not a constant
  expression under clang (breaks godot-cpp's `static_assert`s), and godot-cpp's `/MP` flag is
  reported as unused. `tools/run_clang_tidy.py` passes `-D_CRT_USE_BUILTIN_OFFSETOF` and
  `-Wno-unused-command-line-argument` when it detects `cl.exe`.
