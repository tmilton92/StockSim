# Market Manipulation Simulator — TODO

Educational simulator showing how a brokerage that is also the market maker / manipulator
can move prices (wash trading, stop/liquidation hunting, execution abuse) and extract
capital from retail traders — including the player.

**Stack:** Godot 4.7.2-stable (GDScript UI/visuals) + C++ GDExtension (simulation core)
**Principles:**
- `/core` has zero Godot dependency (headless tests, batch runs)
- Deterministic: seeded RNG, fixed ticks → exact replays, step-back via snapshots
- Every event carries a human-readable reason + optional "broker intent" tag
- All brands/entities fictional; simulated money only

---

## 0. Project setup
- [x] Repo layout: `/core`, `/gdext`, `/game`, `/scenarios`, `/docs` (+ `/third_party`, `/tools`)
- [x] Pin godot-cpp to the 4.7 branch matching Godot 4.7.2-stable
  (godot-cpp 10.0.0-stable built against `extension_api.json` dumped from 4.7.2; ADR 0003)
- [x] Build system (SCons or CMake) for core lib + GDExtension (CMake + presets; ADR 0001)
- [x] Unit test framework for core (Catch2 or GoogleTest) (Catch2 v3; ADR 0002)
- [x] CI: build + run core tests headless (`.github/workflows/ci.yml`: Linux/Windows/macOS
  build + tests, clang-format, clang-tidy, Godot headless import + extension load)
- [ ] CI: confirm the workflow passes on GitHub after the first push (no remote configured yet)
- [x] Code style / clang-format (+ clang-tidy naming rules; ADR 0004)
- [x] project.godot: untyped_declaration, inferred_declaration, unsafe_* warnings set to Error
- [x] Choose GDScript test framework (gdUnit4 or GUT) and record an ADR (gdUnit4; ADR 0008)
- [ ] CI: add a benchmark job once core has a benchmarkable step loop (CLAUDE.md targets)
- [ ] Remove the `extension_list.cfg` first-import workaround in `tools/godot_check.py` once
  the pinned Godot includes the fix for godotengine/godot#123511 (docs/VERSIONS.md)

## 1. Core: order book & matching (C++)
- [x] Types: Price (fixed-point ticks), Qty, AccountId, OwnerId, OrderId, Tick
- [x] Order types: market, limit, stop, stop-limit, cancel, modify
- [x] Limit order book with price-time priority
- [x] Matching engine producing fills
- [x] Last price, best bid/ask, depth snapshot, volume
- [x] Event stream: OrderPlaced, OrderCancelled, Match, Fill, PriceChange (with reason string)
  (schema v1 also has OrderRejected, OrderRested, OrderModified, CommandRejected,
  StopTriggered; ADR 0005)
- [x] Event-level stepping API: `step_event()`, `step_tick()`
- [x] Seeded RNG wrapper used everywhere (xoshiro256**, bit-exact across platforms)
- [x] Tests: crossing, partial fills, priority, stops triggering (+ cancel/modify, validation,
  stepping, randomized invariants and same-seed determinism)
- [ ] Time-in-force options (IOC/FOK/post-only) if a chapter needs them
- [ ] Order book perf pass once benchmarks exist: flat price-indexed ladder instead of
  `std::map` levels (profile first, per CLAUDE.md)

## 2. Snapshots & replay
- [x] Full-state serialization of core (versioned binary format; ADR 0006)
- [x] Snapshot every N ticks
- [x] `step_back()` = restore nearest snapshot + replay forward (plus redo and branching)
- [x] Determinism test: same seed → identical event log hash (pinned golden digest, run on
  all three CI platforms)
- [ ] Bound history memory: snapshot thinning / journal compaction for long sessions
- [ ] Export/import the journal as a replay file (with §12 structured event log export)
- [ ] Include RNG and agent state in snapshots and the journal when agents land (§6)

## 3. GDExtension bridge
- [x] Expose `Simulation` node: init(config), step_event, step_tick, step_back, run(speed)
  (`configure`/`load_scenario`, `run(steps_per_second)` on a worker thread, `pause`; ADR 0007)
- [x] Ring buffer of events pulled by Godot each frame (lock-free SPSC `EventRing` with
  backpressure; `drain_events()` returns packed rows)
- [x] Query API: book depth, order lookup (+ last price, best bid/ask, volume, tick)
- [x] Query API: account state, cash and position (`get_account`, §6a, ADR 0010)
- [ ] Query API: margin and liquidation state once margin exists in core (§7)
- [ ] Query API: ownership lookup once owners exist in core (§8)
- [x] Config from scenario JSON (format v1: `docs/scenario-format.md`, `/scenarios`)
- [x] Decide how scenario files are packaged into exported builds: they live in
  `game/scenarios` (`res://scenarios`) (§5, ADR 0009)
- [x] Typed GDScript wrapper for event rows in the game layer (§4: `SimEvent`)

## 4. Godot: base visualization
- [x] Main layout: chart, order book ladder, trade tape, narration panel, controls
  (`main/main_screen.tscn`; dark terminal theme in `theme/stocksim_theme.tres`)
- [x] Controls: play/pause, step event, step tick, step back, speed slider
- [x] Order book ladder with animated depth bars + fill flashes
- [x] Price chart (candles + last-trade line)
- [x] Trade tape
- [x] Narration panel showing event reasons; highlight related order/account/price
- [x] Inspector panel: click any account or order (and price levels)
- [x] Debug order ticket + "Seed book" so there is a market before agents (stand-in for §10)
- [x] `SimBridge` autoload, typed `SimEvent`/`EventLog` model (closes the §3 wrapper item)
- [ ] Account inspector: cash, position and P&L once accounts exist in core (§7/§10)
- [ ] Cap or window the in-memory event log and rebuild cost for long sessions (with §2 history)
- [ ] Chart: time axis labels, zoom/scroll through history
- [ ] Narration: filter (e.g. hide fill lines) and group events of one step

## 5. Lesson system
- [x] Scenario format: initial state, enabled features, narration cues, pause points
  (scenario format v2 `lesson` object; `docs/scenario-format.md`, ADR 0009)
- [x] Chapter loader + chapter select menu (`ChapterLoader`, `ChapterSelect`, `App` shell)
- [x] Chapter 1: Market creation (empty book, issuer supply, market maker quotes, spread)
- [x] Chapter 2: The first trade (price = last trade, not value)
- [x] Chapter 3: Thin vs deep book (same order, different impact)
- [x] Headless smoke test: every chapter plays to completion and its teaching points hold
- [ ] Export preset that includes `res://scenarios/**/*.json` (verify on first export)
- [ ] Lesson progress persistence (completed chapters) in user://
- [ ] Narration cues tied to specific events within a beat (beyond beat text)

## 6. Agents & cohorts
Split into 6a (core agents), 6b (scale and realism), 6c (Godot and chapter 4). ADR 0010.

- [x] Account ledger in core: cash/positions, long-only limits with leverage, commitments,
  conservation check (6a; margin and shorting in §7)
- [x] Fundamental value process (hidden true value)
- [x] Agent SoA storage in C++
- [x] Trader styles: momentum, mean-reversion, FOMO/herding, panic seller, leverage gambler, value, noise
- [x] Per-agent params: capital, leverage, risk tolerance, reaction delay, attention, stop habits, social sensitivity
- [x] Agent world in snapshots/save-load; tick-start decision phase; pinned agent golden digest
- [ ] Cohort agents (weighted representative agents) for 1M → billions
- [x] Market maker agent (two-sided quotes, inventory mgmt)
- [ ] Arbitrageur agent (toggleable)
- [ ] Threading for agent decision phase
- [ ] Sanity checks: fat tails, volatility clustering
- [ ] Godot: trader-count slider (1 → 10 → 1k → 1M → billions), sampled agent dots sized by weight
- [ ] Chapter 4: Growing the crowd

## 7. Futures layer
- [ ] Contract spec: size, tick, expiry, settlement index
- [ ] Initial/maintenance margin, mark-to-market each tick/day
- [ ] Open interest tracking
- [ ] Margin calls + liquidation engine (forced market orders)
- [ ] Liquidation cascade detection/event tagging
- [ ] Godot: liquidation markers on chart, cascade timeline
- [ ] Chapter 5: Futures & leverage

## 8. Manipulator accounts & wash trading
- [ ] Owner entity controlling N accounts
- [ ] Wash trade action (self-matching across own accounts)
- [ ] Painting the tape (laddered prints)
- [ ] Spoofing / layering (place + cancel)
- [ ] Accumulate → pump → distribute playbook
- [ ] Net-capital tracker per owner (shows wash loop ≈ 0 net minus fees)
- [ ] Godot: ownership view toggle (color by true owner on tape/chart)
- [ ] Godot: account graph (nodes = accounts, edges = trades, wash cycles glow)
- [ ] Godot: capital flow particles between pools
- [ ] Chapter 6: Wash trading — price moves, no capital changes hands

## 9. Broker layer (broker = manipulator)
- [ ] `Broker` sits between clients and book: routing (internalize/B-book vs pass-through)
- [ ] Full visibility API: all client orders, stops, leverage, margin, liquidation prices
- [ ] Front-running client orders
- [ ] Targeted stop / liquidation hunting using exact positions
- [ ] Execution abuse toggles: spread widening, asymmetric slippage, last-look rejects, ack delays
- [ ] Broker price feed with controllable deviation from market
- [ ] Mid-trade margin/leverage rule changes
- [ ] Broker AI: score accounts by extractable value, pick targets
- [ ] "Broker intent" tags on all broker events
- [ ] Godot: broker console / x-ray overlay (heat map of stops + liquidation prices on price axis)
- [ ] Godot: Sankey of capital flow retail → broker
- [ ] Chapter 7: The x-ray view
- [ ] Chapter 8: The house is the broker

## 10. Player mode
- [ ] Player as a normal agent driven by UI input (no special-casing in core)
- [ ] Account opening flow: balance, leverage, account type; fictional-brand "aggressive marketing" screen
- [ ] Trading UI: chart, order ticket, positions, P&L, margin meter
- [ ] Locked broker view (unlocks on blow-up or sandbox toggle)
- [ ] Post-mortem replay: player timeline side-by-side with broker console + intent narration
- [ ] Loss attribution: spread, slippage, front-run, stop hunt, liquidation, fees → % to broker
- [ ] Honest-broker counterfactual: replay player's exact orders through a regulated pass-through broker
- [ ] Difficulty presets (broker aggression levels)
- [ ] Chapter 9: Your account

## 11. Safeguards & comparison
- [ ] Self-trade prevention
- [ ] Beneficial-ownership linking
- [ ] Cancel-to-fill ratio alerts, volume vs OI anomalies, position limits
- [ ] Segregated client funds, best-execution rule, no internalization mode
- [ ] Side-by-side replay: same seed, safeguards off vs on
- [ ] Chapter 10: Safeguards

## 12. Polish & tooling
- [ ] Scenario editor (or at least hot-reloadable JSON)
- [ ] Headless batch runner + CSV export of metrics
- [ ] Metrics dashboard: price vs value, real vs reported volume, wealth transfer, P&L by style
- [ ] Performance profiling at 1M agents / billions via cohorts
- [ ] Glossary / tooltips for terms
- [ ] Docs: architecture, adding a trader style, adding a chapter
