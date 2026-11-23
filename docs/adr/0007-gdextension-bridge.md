# ADR 0007: GDExtension bridge, worker thread, and scenario JSON v1

- Status: Accepted
- Date: 2026-11-23

## Context

TODO §3 exposes the simulation to Godot: a `Simulation` node with stepping, time travel and
continuous running, an event buffer drained each frame, queries, and configuration from a
scenario file. Decisions taken with the user before implementation:

- events reach GDScript as packed integer arrays, not one object per event;
- account-state and ownership queries wait until accounts and owners exist in core (§7/§8/§10);
- `run()` uses a worker thread from the start, with a lock-free ring buffer;
- configuration comes from scenario JSON now (format v1), not a Godot resource.

## Decision

### Node API (`gdext/src/simulation_node.*`, documented in `gdext/doc_classes/Simulation.xml`)

- One class, `Simulation` (extends `Node`), registered in `register_types.cpp`.
- Inputs: `submit_order`, `cancel_order`, `modify_order` (ids and prices as `int`; side and
  order type as bound enums `Simulation.Side` / `Simulation.OrderType`).
- Stepping: `step_event`, `step_tick`, `step_back` (+ `rewound(last_sequence)` signal),
  `can_step_back`, `has_redo`; continuous: `run(steps_per_second)` (0 = unthrottled), `pause`.
- Events: `drain_events(max)` returns rows of `EVENT_STRIDE` (15) `int64` values, laid out per
  the `EVENT_FIELD_*` constants, mirroring event schema v1 (ADR 0005). `reason_text()` and
  `event_type_name()` give narration text. Godot-side enums mirror core enums exactly, enforced
  by `static_assert`s.
- Queries: last price, best bid/ask (`has_*` + `get_*`, since prices are integers with no
  sentinel), volume, trade count, tick, `get_depth` (rows of `DEPTH_STRIDE`), `get_order` (an
  `ORDER_STRIDE` row or empty).
- Persistence: `save_state` / `load_state` (bytes from `stocksim::Simulation::save`).
- Argument errors use `ERR_FAIL_*_MSG` (logged, safe return value); no exception crosses the
  boundary.

### Threading

- Core gains `EventRing`: a fixed-capacity, lock-free SPSC ring with all-or-nothing pushes,
  tested single-threaded and with a two-thread stress test.
- Core gains `kMaxEventsPerStep` (4), asserted in `MatchingEngine::step_event`, and
  `Simulation::advance()` (one step, or the tick advance when idle).
- The node holds one mutex around every use of the core simulation. The producer (worker, or the
  main thread when stepping manually) always pushes while holding it and only steps when the
  ring has `kMaxEventsPerStep` free slots: backpressure, never dropped events. The consumer is
  always the main thread (`drain_events`, and `step_back`, which removes events of the undone
  future from the ring).
- Unthrottled running takes 256 steps per lock hold, then yields, so main-thread calls are not
  starved. Paced running uses `steady_clock` only to decide when to step; results depend only
  on inputs and their order, never on timing.
- The worker starts on the first `run()` and is joined in `_exit_tree` and the destructor.

### Scenario JSON v1 (`docs/scenario-format.md`, files in `/scenarios`)

- Parsed with Godot's `JSON` class in gdext, so core stays dependency-free; core's
  `validate(SimulationConfig)` holds the authoritative limits.
- Strict: unknown keys, wrong types, fractional numbers, out-of-range values, and other
  `format`/`format_version` values are errors with a precise message.
- v1 configures the simulation and runtime buffer only; lesson content comes with §5 as v2.

### Build

- On MSVC, godot-cpp defines `_HAS_EXCEPTIONS=0` publicly. The extension links a core variant
  (`stocksim_core_gdext`) compiled with the same define, avoiding an ODR violation in the STL.
- Class reference XML is generated from the loaded extension (`--doctool --gdextension-docs`)
  and filled from `Simulation.docs.json` by `tools/fill_class_docs.py`; `tools/godot_check.py`
  fails if the committed XML is out of date or anything is undocumented.

### Testing

- Core: ring buffer, `advance()`, per-step event bound, config validation (Catch2).
- Godot: `game/tests/smoke/simulation_smoke.gd`, run headless by `tools/godot_check.py`,
  exercises every method, the worker, pacing, backpressure, time travel and persistence. Errors
  triggered deliberately are announced (`EXPECT_ERROR:`) and matched; any other error or warning
  fails the check. This is an interim harness until a GDScript test framework is chosen (TODO
  §0).

## Consequences

- GDScript code reads events by index constants; the game layer (§4) should wrap rows in a
  small typed class for readability.
- Queries while running see the state at call time, which may be ahead of drained events.
- Event schema or reason changes must update the Godot enum mirrors, the docs JSON and the
  smoke test (the `static_assert`s and doc check catch most omissions).
- Account and ownership queries are tracked in TODO §3 for when those features exist.
