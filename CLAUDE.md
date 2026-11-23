# CLAUDE.md

Guidance for Claude when working in this repository. Read this file and `TODO.md` at the start of every session.

## Project overview

An educational market simulator that shows, step by step, how a brokerage acting as its own
market maker and manipulator can move prices (wash trading, spoofing, stop/liquidation hunting,
execution abuse) and extract capital from retail traders, including the player.

- **Engine:** Godot 4.7.2-stable (GDScript for UI, visuals, lessons, player interaction)
- **Simulation core:** C++ (plain library) exposed to Godot via GDExtension (godot-cpp)
- **Roadmap:** `TODO.md` is the source of truth for scope and order of work

**Treat this as a production application.** No throwaway code, no "TODO: fix later" without a
matching TODO.md entry, no skipped tests, no silenced warnings.

## Repository layout

```
/core        C++ simulation library. NO Godot dependency. Headless-testable.
/core/tests  Unit + determinism tests for core
/gdext       GDExtension bindings: thin wrappers translating core <-> Godot types
/game        Godot project (project.godot lives here)
/scenarios   Lesson/chapter definitions (JSON or .tres)
/docs        Architecture notes, ADRs, how-to guides
/tools       Batch runner, scripts, CI helpers
/third_party Pinned git submodules (godot-cpp, Catch2); never edited locally
```

## Workflow rules for Claude

1. **Start of task:** read `TODO.md`, find the item(s) being worked on, confirm scope.
2. **Check the docs before writing engine code.** Godot APIs change between minor versions and
   training data may be outdated. Before using any Godot class, method, signal, annotation,
   or project setting you have not verified in this session:
   - Use the **4.7** docs, not `latest` (which documents unreleased features):
     https://docs.godotengine.org/en/4.7/
   - Class reference: https://docs.godotengine.org/en/4.7/classes/
   - GDScript style guide: https://docs.godotengine.org/en/4.7/tutorials/scripting/gdscript/gdscript_styleguide.html
   - Static typing & warnings: https://docs.godotengine.org/en/4.7/tutorials/scripting/gdscript/static_typing.html
   - Best practices: https://docs.godotengine.org/en/4.7/tutorials/best_practices/index.html
   - GDExtension C++: https://docs.godotengine.org/en/4.7/tutorials/scripting/cpp/index.html
   - godot-cpp on the branch/tag matching 4.7: https://github.com/godotengine/godot-cpp
   - If an API only appears in `latest` (4.8+), do not use it.
   - Prefer the documented, idiomatic approach over clever workarounds. If docs and memory disagree, docs win.
3. **Small, reviewable changes.** One TODO item (or a clear sub-part) per change set.
4. **Tests with every change** to `/core`. No core feature is done without tests.
5. **Update `TODO.md`** checkboxes when items are completed; add new items you discover rather
   than leaving untracked work.
6. **Ask before** changing architecture, public APIs between core/gdext/game, the scenario
   format, the event schema, or adding third-party dependencies. Record accepted decisions as
   short ADRs in `/docs/adr/`.
7. **Never** commit build artifacts, `.godot/` cache, or binaries (keep `.gitignore` current).
8. **Verify before claiming done:** build succeeds, tests pass, the Godot project opens without
   errors or warnings in the Output/Debugger panels.

## Engine version notes

- Target: **Godot 4.7.2-stable**. Record the exact build in `/docs/VERSIONS.md` and keep
  godot-cpp on the matching 4.7 branch/commit. Upgrade both together, never one alone.
- Engine upgrades (e.g. to 4.8) only via an ADR after the target is stable, with a full test pass.
- Regenerate `extension_api.json` from the pinned engine when upgrading.
- Note any engine regressions or workarounds in `/docs/VERSIONS.md` with links to the issue.

## Architecture rules

### Boundaries
- `/core` knows nothing about Godot. Standard C++ only.
- `/gdext` is a thin translation layer. No simulation logic in it.
- `/game` never computes market logic. It calls the `Simulation` node and renders results.
- Data flows one way per frame: Godot calls `step_*` → core emits events into a ring buffer →
  Godot drains events and updates visuals.

### Determinism (non-negotiable)
- All randomness goes through the seeded RNG in core. No `rand()`, `std::random_device`,
  wall-clock time, or unordered-container iteration order affecting results.
- Prices are **fixed-point integer ticks** (`int64_t`). Money/quantities are integers in
  smallest units. **No floating point for prices, balances, or P&L.** Floats are allowed only
  for agent heuristics and must not affect ordering ties nondeterministically.
- Multithreaded phases must produce identical results regardless of thread count (deterministic
  partitioning + ordered merge).
- CI runs a determinism test: same seed + config → identical event-log hash.

### Events
- Every state change emits an event with: tick, sequence number, type, involved ids, and a
  human-readable `reason`. Broker actions also carry an `intent` tag.
- The event schema is versioned. Changing it requires an ADR and updating the narration layer.

### Player parity
- The player is a regular account in core. No special cases. The broker treats the player like
  every other client.

## C++ standards (`/core`, `/gdext`)

- C++20. Compile with `-Wall -Wextra -Wpedantic -Werror` (MSVC: `/W4 /WX`).
- `clang-format` (config in repo root) and `clang-tidy` must pass.
- RAII everywhere; no raw `new`/`delete` in core. Prefer value types, `std::unique_ptr`,
  `std::span`, `std::optional`, `std::expected` (or project `Result<T>`) for fallible ops.
- No exceptions crossing the GDExtension boundary. Convert errors to Godot errors in `/gdext`.
- Strong typedefs for ids and units (`AccountId`, `OwnerId`, `PriceTicks`, `Qty`) so they can't
  be mixed up.
- Hot paths: structure-of-arrays for agents, no allocations per tick, no virtual calls per
  order in the matching loop. Profile before optimizing anything else.
- Naming: `snake_case` functions/variables, `PascalCase` types, `kConstant` or `UPPER_CASE`
  constants (pick one in `.clang-format`/tidy config and enforce), `m_` or trailing `_` for
  members (enforced by tidy).
- Headers: `#pragma once`, include what you use, forward-declare where practical.

### GDExtension specifics
- Register classes in a single `register_types.cpp`. Use `GDCLASS`, `_bind_methods`,
  `ClassDB::bind_method` with `D_METHOD` argument names so the editor shows proper docs.
- Write XML class docs for every exposed class (`doc_classes/`) so they appear in the editor help.
- Use `Ref<>` for `RefCounted` types; never hold raw pointers to Nodes across frames.
- Never touch the SceneTree or Nodes from worker threads. Simulation threads only write to the
  ring buffer; the main thread reads it.
- Expose bulk data (book depth, agent samples) as `PackedInt64Array`/`PackedFloat32Array`, not
  `Array` of `Dictionary`, for performance.
- Emit Godot signals only from the main thread.

## GDScript standards (`/game`)

Follow the official GDScript style guide exactly. Key points:

- **Static typing everywhere, enforced as errors.** Typed variables, parameters, return types,
  typed arrays/dicts. These warnings are set to **Error** in `project.godot` and must stay that way:

  ```ini
  [debug]
  gdscript/warnings/untyped_declaration=2
  gdscript/warnings/inferred_declaration=2
  gdscript/warnings/unsafe_property_access=2
  gdscript/warnings/unsafe_method_access=2
  gdscript/warnings/unsafe_cast=2
  gdscript/warnings/unsafe_call_argument=2
  ```
  (verify the setting names against the 4.7 docs when creating the project)

  Consequences:
  - No `:=`. Always write the type: `var price: int = 0`.
  - No untyped `Variant` flowing into typed code. Cast explicitly with `as` and check for `null`,
    or use `is` before accessing members.
  - Get nodes with typed references: `@onready var _ladder: OrderBookLadder = %OrderBookLadder`.
  - Dictionary/Array access from untyped sources must be cast before use. Prefer typed
    `Array[T]` / `Dictionary[K, V]` and custom `Resource`/`RefCounted` classes over raw dicts.
  - Never disable these per-file or with `@warning_ignore` unless unavoidable, and then with a
    comment explaining why (third-party addons are excluded via `debug/gdscript/warnings/directory_rules`, which
    replaced `exclude_addons` in 4.7 and excludes `res://addons` by default).
- Other warnings stay at their defaults (Warn) and must be fixed, not ignored.
- Naming: `snake_case` files, functions, variables; `PascalCase` class_name and node names;
  `CONSTANT_CASE` constants and enum members; `_private` prefix for private members; signals in
  past tense (`order_filled`, `account_liquidated`).
- Script member order (per style guide): `@tool`, `class_name`, `extends`, doc comment, signals,
  enums, constants, `@export` vars, public vars, private vars, `@onready` vars, `_init`,
  `_enter_tree`, `_ready`, other built-in virtuals, public methods, private methods, subclasses.
- Tabs for indentation, 100-char soft line limit, two blank lines between functions.
- Use `##` doc comments on public classes, methods, signals, and exports.
- Use `%UniqueName` for node references inside a scene; avoid long `get_node("../../..")` paths.

### Scene & node practices
- **Composition over inheritance.** Small, focused scenes (OrderBookLadder, PriceChart,
  TradeTape, NarrationPanel, BrokerConsole, AccountGraph) instanced into larger ones.
- **Call down, signal up.** Parents call methods on children; children emit signals. Siblings
  communicate via their parent or a dedicated event bus, never by reaching into each other.
- Autoloads only for true globals (e.g. `SimBridge`, `Settings`, `LessonRunner`). Keep them few
  and documented.
- Data lives in `Resource` subclasses (`ScenarioConfig`, `TraderStyle`, `ChapterDef`), not in
  scene scripts.
- Each scene must run standalone with F6 using mock data where practical.
- Use `Control` containers and anchors for UI layout; support resizing and multiple resolutions.
  Use a project `Theme` resource, not per-node overrides.
- Rendering many points (agents, particles): use `MultiMeshInstance2D`, `GPUParticles2D`, or
  custom drawing in `_draw()`. Never one Node per agent.
- Do per-frame work in `_process` only when needed; disable processing on idle nodes.

## Testing

- **Core:** GoogleTest or Catch2 (choose once, record in an ADR). Cover matching, priorities,
  stops, margin, liquidation, broker policies, snapshot/restore, determinism.
- **Property/fuzz tests** for the order book (invariants: no crossed book after matching,
  conservation of quantity and cash, every fill has two sides).
- **Conservation check:** total cash + margin across all accounts plus fees equals initial
  total, every tick, in debug builds.
- **GDScript:** gdUnit4 or GUT for UI logic and lesson runner (choose once, ADR).
- **Scenario smoke tests:** each chapter runs headless to completion without errors.
- **Benchmarks:** track ticks/sec at 1k, 100k, 1M agents; flag regressions over 10%.

## Error handling & logging

- Core: return errors, don't crash. Assert on internal invariants in debug builds.
- Godot: use `push_error`/`push_warning` with clear context; `ERR_FAIL_*` macros in C++ bindings.
- No silent failures. No empty `except`/`catch` blocks.
- Structured event log can be exported to file for debugging and replay.

## Performance targets (initial; revisit with profiling)

- Step-through mode: instant response per event.
- 100k individual agents at ≥ 60 sim ticks/sec on a modern desktop.
- 1M individual agents usable at reduced tick rate; billions via cohorts.
- UI holds 60 FPS regardless of sim speed (sim runs decoupled from render).

## Git & CI

- Conventional commits (`feat(core): ...`, `fix(game): ...`, `test(core): ...`, `docs: ...`).
- Feature branches, PRs into `main`. `main` is always buildable.
- CI: build core + gdext on Linux/Windows/macOS, run core tests, run determinism test, run
  clang-format/tidy check, run Godot headless import + GDScript tests + scenario smoke tests.

## Definition of done

- [ ] Builds clean with warnings-as-errors
- [ ] Tests added/updated and passing (including determinism)
- [ ] Godot docs checked for any engine API used
- [ ] Style guides followed (clang-format, GDScript style guide, static typing)
- [ ] Public APIs and exposed classes documented
- [ ] No new editor warnings or errors
- [ ] `TODO.md` updated

## Content guidelines

- All companies, brokers, and brands in the game are fictional. Do not use real broker,
  exchange, or ticker names or logos.
- All money is simulated. No real-market data feeds, brokerage APIs, or order routing.
- The goal is education: every manipulation mechanic should be paired with the safeguard that
  detects or prevents it (see TODO §11). Do not add features aimed at evading real-world
  surveillance, KYC, or regulation.
