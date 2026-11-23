# ADR 0002: Catch2 v3 for core unit tests

- Status: Accepted
- Date: 2026-11-23

## Context

`/core` needs unit, property/fuzz-style (order-book invariants), and determinism tests, plus
light micro-benchmarks. Candidates were GoogleTest and Catch2.

## Decision

Use Catch2 v3 (`Catch2::Catch2WithMain`), registered with CTest via `catch_discover_tests`.

- Generators (`GENERATE`, `take`, `random`) cover property-style tests. Any randomness used in
  tests must be seeded explicitly so test runs are deterministic.
- `BENCHMARK` covers micro-benchmarks; the ticks/sec suite will be a separate tool.
- Catch2 is built in-tree as C++20; it must match the tests' standard or its
  `string_view`/`span` support is compiled out (seen as a link error on MSVC).

## Consequences

- No gMock; mocks, where needed, are hand-written fakes. The core is built around value types
  and interfaces we own, so this is acceptable.
- The GDScript test framework (gdUnit4 vs GUT) is a separate decision with its own ADR.
