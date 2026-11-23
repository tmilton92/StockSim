# Building

## Prerequisites

- Git. Clone with submodules (`git clone --recurse-submodules ...`), or run
  `git submodule update --init --recursive` in an existing clone.
- CMake 3.25+ and Ninja.
- Windows: Visual Studio 2022 or Build Tools with the "Desktop development with C++" workload
  (bundles CMake, Ninja, clang-format, clang-tidy).
- Linux: GCC 13+ or Clang 17+. macOS: Xcode 16 / Apple Clang.
- Python 3.10+ for the scripts in `/tools`.
- Godot **4.7.2-stable** (see `docs/VERSIONS.md`).

## Windows (any PowerShell prompt)

```powershell
./tools/build.ps1 -Preset debug        # configure + build + run core tests
./tools/build.ps1 -Preset core-only    # core + tests only, skips godot-cpp
./tools/build.ps1 -Preset release -NoTest
```

## Linux / macOS

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

The `debug` preset writes `game/bin/libstocksim.<platform>.template_debug.<arch>`, which
`game/bin/stocksim.gdextension` loads. Open `game/project.godot` in Godot 4.7.2.

## Checks (same as CI)

```sh
python tools/check_format.py [--fix] [--clang-format PATH]
python tools/run_clang_tidy.py --build-dir build/debug [--clang-tidy PATH]
python tools/godot_check.py --godot PATH_TO_GODOT
```
