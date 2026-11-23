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
- **clang-tidy 19 `bugprone-exception-escape.IgnoredExceptions` is comma-separated**, unlike
  most list options, which accept `;`. A `;`-separated value silently matches nothing.
- **MSVC debug CRT reports failed `assert()`s in a dialog box**, so a failing assert in a test
  run aborts silently (exit code 3). `core/tests/crt_report_mode.cpp` routes CRT assert/error
  reports to stderr in debug test builds.
- **godot-cpp `target_doc_sources()` called from another directory** builds a command without
  the Python interpreter (`Python3_EXECUTABLE` is only set in godot-cpp's directory scope), and
  the build fails with "CreateProcess failed". `gdext/CMakeLists.txt` calls
  `find_package(Python3)` itself before using it.
- **godot-cpp on MSVC defines `_HAS_EXCEPTIONS=0` publicly** for every target linking it. Core
  code linked into the extension is therefore built as a separate variant
  (`stocksim_core_gdext`) with the same define, avoiding an ODR violation in the STL.
- **MSVC C4714 in release builds of the extension:** godot-cpp's force-inline `memnew` helpers
  trigger "marked as __forceinline not inlined" when instantiated in our code. godot-cpp builds
  itself with `/wd4714` but keeps it private; `gdext/CMakeLists.txt` applies the same flag to
  the extension target only.
- **Godot 4.7.x crashes at exit on the first import of a project that contains a GDExtension**
  (0xC0000005 on Windows, SIGSEGV elsewhere). When `.godot/extension_list.cfg` does not exist
  yet, the first filesystem scan discovers the extension and queues a doc regeneration that
  runs during shutdown, after teardown. Reproduced here: a fresh import crashes whenever the
  extension registers a class or embeds docs. Upstream:
  [godotengine/godot#123511](https://github.com/godotengine/godot/issues/123511),
  [#125063](https://github.com/godotengine/godot/issues/125063), fix proposed in
  [#125566](https://github.com/godotengine/godot/pull/125566) (unmerged as of 2026-11-23).
  Workaround: `tools/godot_check.py` writes `.godot/extension_list.cfg` before importing, so the
  extension loads at startup (3/3 fresh imports clean). Opening a fresh clone in the editor GUI
  can still crash once on exit; reopening works. Remove the workaround when the engine pin
  moves to a release containing the fix.
