# ADR 0010: Account ledger, event schema v2, and the agent world

- Status: Accepted
- Date: 2026-11-23

## Context

TODO §6 adds a simulated crowd. Agents need money and shares that can run out, or they would
trade without limit and the market would mean nothing. §6 was split into three parts with the
user; this ADR covers part 6a: the hidden value process, structure-of-arrays agent storage, the
seven trader styles and their parameters, the market maker, and agent state in snapshots.
Decisions taken with the user:

- add an account ledger to core now (margin and liquidation still come in §7);
- agents act in a decision phase at the start of each tick.

## Decision

### Account ledger (`core/include/stocksim/account_ledger.hpp`)

- `MatchingEngine` owns an `AccountLedger`. Every account that trades is tracked (cash and
  position as integers, `Money` = price ticks x shares). Only accounts *opened* with
  `open_account(id, cash, shares, leverage_percent)` are **limited**; scripted participants,
  lessons and the sandbox ticket keep trading unlimited, so chapters 1-3 are unchanged.
- Limits until §7: long-only with leverage. A buy needs `exposure + cost <= equity x leverage`
  (mark = last trade price); a sell needs `position - committed_sell >= qty`. Resting buy limits
  reserve their notional, resting sells and pending sell stops reserve their shares, for every
  account (so the books always agree), but only limited accounts are checked.
- Orders that fail a check are rejected (or, for a modify or a triggered stop, refused/cancelled)
  with a reason. Cash and shares only move between accounts: `check_conservation()` runs after
  every tick in debug builds, and the property test checks commitments against the book.

### Event schema v2 and state format v2

- `kEventSchemaVersion = 2`: two reasons appended, `kInsufficientFunds` and
  `kInsufficientShares`. No field changed. The narration layer uses `reason_text()`, so it needs
  no table update; the bridge mirrors the new `REASON_*` constants with `static_assert`s.
- `kStateFormatVersion = 2`: the engine state adds per-tick taker volume and the ledger; a
  simulation state adds a flag and the agent world after the engine. Golden digests were
  re-pinned (the schema version is hashed).
- The bridge gains `get_account(id)` (packed `ACCOUNT_FIELD_*` row), closing the deferred §3
  account query for cash and position.

### Agent world (`core/include/stocksim/agents/`)

- `AgentWorld` holds the `ValueProcess` (bounded random walk with rare jumps; always 4 draws per
  tick), an `AgentPopulation` stored as structure of arrays, and an optional `MarketMaker`. It
  is configured by `SimulationConfig::agents` (absent by default) and validated with the rest.
- `Simulation::advance_tick()` is the single place ticks advance: the engine advances, then the
  world runs `on_tick_start`. Agents' orders are submitted like anyone else's and processed in
  the tick's steps. Replays re-run the same code from a snapshot, so live play, step-back/redo
  and save/load agree exactly.
- Every agent is an ordinary limited account (player parity). `decide()` is a pure function per
  style (noise, momentum, mean reversion, herding, panic seller, leverage gambler, value) with
  integer heuristics; per-agent parameters (capital, shares, leverage, attention, risk, reaction
  delay, stop habit and distance, social sensitivity) are drawn once from the seed.
- The market maker re-quotes each tick: `levels` prices each side of the reference, skewed one
  tick per `skew_per_tick` shares of inventory away from its starting position.

### Determinism rules (added)

- One world draw seeds each tick; agent *i* derives its own stream from it, so an agent's
  choices do not depend on how many others exist or their order (ready for §6b threading).
- Never make two RNG draws in one expression or argument list: evaluation order is unspecified
  and differs between compilers (it broke the macOS golden once). Draw into named locals.
- Core compiles with `-ffp-contract=off` on GCC/Clang. Agents use no floating point.
- A second pinned golden (`agents_test.cpp`) hashes a 200-tick agent session on every platform.

## Consequences

- Lessons and the sandbox behave as before; agents appear only when a config asks for them.
  Exposing agents to scenario JSON and the game (crowd slider, dots, chapter 4) is §6c and will
  need a scenario-format change, decided then.
- Cohorts, the arbitrageur, threading of the decision phase, statistical sanity checks and
  benchmarks are §6b.
- Short selling, margin calls and liquidation replace the long-only rule in §7.
