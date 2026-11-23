# ADR 0004: C++ formatting and naming conventions

- Status: Accepted
- Date: 2026-11-23

## Context

CLAUDE.md leaves two choices open: `kConstant` vs `UPPER_CASE` constants, and `m_` vs a
trailing `_` for members. Both must be enforced by tooling.

## Decision

- Formatting: `.clang-format` (LLVM base, 4-space indent, 100 columns, left pointer alignment,
  include groups: own, then godot-cpp, then third-party, then std). Pinned clang-format
  **19.1.5**.
- Naming, enforced by `readability-identifier-naming` in `.clang-tidy`:
  - types, enums, aliases, template parameters: `PascalCase`
  - functions, methods, variables, parameters, public members: `snake_case`
  - private/protected members: `m_` prefix (`m_best_bid`)
  - constants (`constexpr`, global/static const) and enum constants: `kPascalCase`
  - macros: `UPPER_CASE`
- clang-tidy runs with warnings-as-errors over `/core` and `/gdext` via
  `tools/run_clang_tidy.py`; third-party headers are excluded.

## Consequences

- Formatting drift between clang-format versions is avoided by pinning the version in CI
  (`pip install clang-format==19.1.5`) to match the MSVC-bundled LLVM 19.1.5.
- godot-cpp's own API uses Godot naming (`p_arg`, `_bind_methods`); that is accepted in
  `/gdext` where we override or call into it.
- There is no CMake formatter in CI yet; CMake files follow 4-space indentation by convention.
