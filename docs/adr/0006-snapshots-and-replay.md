# ADR 0006: State serialization, snapshots, step-back and determinism checks

- Status: Accepted
- Date: 2026-11-23

## Context

TODO §2 needs full-state serialization, periodic snapshots, `step_back()`, and a determinism
test. The UI steps one engine step at a time (ADR 0005) and must be able to step backwards at
the same granularity, then forwards again, without the simulation diverging.

## Decision

### Binary state format (`kStateFormatVersion = 1`)

- `StateWriter`/`StateReader` (`stocksim/serialization.hpp`) encode fixed-width little-endian
  integers byte by byte, so output is identical on every platform. The reader is bounds-checked
  with a sticky failure flag, and element counts are checked against the remaining bytes before
  anything is allocated.
- `MatchingEngine::save_state` writes counters and market data, resting orders per side in
  priority order (re-adding them in that order rebuilds identical queues), the active order,
  triggered stops, pending stops per side with their trigger sequence, and queued requests.
  Undrained events are not state. `load_state` validates enums, quantities, prices, duplicate
  ids, and that the book is not crossed, returning `Result<MatchingEngine>`
  (`stocksim/result.hpp`, a stand-in for C++23 `std::expected`).
- `Simulation::save()` prefixes a header: magic `"SSIM"`, `kStateFormatVersion`,
  `kEventSchemaVersion`. Loading rejects other versions rather than migrating; migrations can
  be added when saved games need to survive upgrades.

### Journal, snapshots, step-back (`stocksim/simulation.hpp`)

- `Simulation` wraps the engine and journals every operation: submit/cancel/modify, each
  engine step, and each tick advance (`MatchingEngine::advance_tick` was split out of
  `step_tick` for this).
- A snapshot (serialized engine state) is kept at position 0 and after every tick advance that
  lands on a multiple of `snapshot_interval_ticks` (default 64; 0 disables periodic ones).
- `step_back()` targets the position just before the most recent step or tick advance (inputs
  given after that step are undone with it), restores the nearest snapshot at or before it, and
  replays the journal forward with events discarded. Cost is bounded by the snapshot interval.
- After stepping back, `step_event`/`step_tick` redo the recorded future, re-emitting identical
  events with identical sequence numbers. Any new input first discards the recorded future and
  the snapshots taken in it (branching, as in editor undo).
- After `step_back()` the event buffer is empty; consumers drop events with
  `seq > last_sequence()`.

### Determinism checks

- `EventLogHasher` is a 64-bit FNV-1a over each event field as fixed-width little-endian bytes
  (never raw struct memory, whose padding is unspecified), seeded with `kEventSchemaVersion`.
  Its encoding is pinned by vectors computed with an independent Python implementation.
- Tests assert that the same seed gives the same digest, different seeds differ, and a pinned
  golden digest for one seeded session that mixes inputs, steps, ticks and time travel. CI runs
  it on Linux, Windows and macOS, in debug; it was also verified locally in release.

## Consequences

- History is in memory only and grows with the session (journal + snapshots); `save()` writes
  the current state without history. Bounding retention and exporting the journal for replay
  files are tracked in TODO.md.
- Any change to the engine state layout bumps `kStateFormatVersion`; any intended behaviour
  change updates the pinned golden digest in the same commit, with the reason stated.
- Agents (TODO §6) will add RNG and agent state to the snapshot; the journal must then also
  capture whatever drives them (seed and config) so replays stay exact.
