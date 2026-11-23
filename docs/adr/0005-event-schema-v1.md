# ADR 0005: Event schema v1 and the step model

- Status: Accepted
- Date: 2026-11-23

## Context

Every state change in core must emit an event carrying tick, sequence number, type, involved
ids, a human-readable reason, and (for broker actions) an intent tag. Events feed the Godot
ring buffer (TODO §3), the narration layer, replay/step-back (§2), and determinism hashing.
The UI must also be able to step through a single order's sweep one fill at a time.

## Decision

### Shape

`stocksim::Event` (`core/include/stocksim/event.hpp`) is one flat, fixed-size, trivially
copyable struct, `kEventSchemaVersion = 1`:

| Field | Meaning |
| ----- | ------- |
| `seq` | Global sequence number, starts at 1, +1 per event, never reused |
| `tick` | Simulation tick when emitted |
| `type` | `EventType` |
| `reason` | `Reason` code; `reason_text(reason)` gives fixed English text (no allocation) |
| `intent` | `BrokerIntent`; `kNone` until the broker layer (§9) exists |
| `side`, `order_type` | Of `order_id` |
| `order_id`, `account` | Primary order and its account |
| `counter_order_id`, `counter_account` | The other side, where there is one |
| `price`, `aux_price`, `qty`, `remaining` | Per type, below |

Reason codes are an enum rather than strings so emitting never allocates; the narration layer
composes richer sentences from the ids and prices in the event.

### Per-type field use

| Type | `price` | `aux_price` | `qty` | `remaining` |
| ---- | ------- | ----------- | ----- | ----------- |
| OrderPlaced | limit price (0 if none) | stop price (0 if none) | order qty | order qty |
| OrderRejected | as submitted (normalized) | as submitted | as submitted | 0 |
| OrderRested | resting price | 0 | qty joining the book | same |
| OrderCancelled | order's price | stop price (pending stops) | qty removed | 0 |
| OrderModified | new price | previous price | previous remaining | new remaining |
| CommandRejected | requested price (modify) | 0 | requested qty (modify) | 0 |
| StopTriggered | last trade price that fired it | stop price | qty | qty |
| Match | execution price | 0 | executed qty | 0 |
| Fill | execution price | 0 | executed qty | this order's remaining after |
| PriceChange | new last price | previous last price (0 if first) | 0 | 0 |

- A `Match` (taker = `order_id`, maker = `counter_order_id`) is always followed by exactly two
  `Fill` events, taker first then maker, then a `PriceChange` if the last price moved.
- Executions are at the maker's price.
- Unused price fields of a request (e.g. the limit price on a market order) are normalized to 0.

### Step model

Requests are queued; `step_event()` performs one atomic step that emits at least one event and
leaves engine state consistent with the events emitted so far:

1. accept/reject one request;
2. one execution of the active order (Match, Fill, Fill, [PriceChange]);
3. rest or cancel the active order's remainder;
4. activate one triggered stop.

`step_tick()` runs steps until idle and then advances the tick. Stops are checked when the
active order finishes, not mid-sweep. Triggered stops queue buy-side first (lowest stop first),
then sell-side (highest stop first), ties by placement order, and all run before the next
request. Self-matching is allowed until self-trade prevention (§11) exists.

## Consequences

- Any change to fields, types, reason codes, or their meaning bumps `kEventSchemaVersion`
  and needs a new ADR plus narration updates (CLAUDE.md).
- Events are about 80 bytes; a fixed struct keeps the ring buffer and hashing simple at the
  cost of some unused fields per type.
- Reason text is English-only for now; localization would map codes to translated strings in
  the game layer, without a schema change.
