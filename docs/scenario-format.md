# Scenario format (versions 1 and 2)

Scenarios are JSON files in `game/scenarios` (`res://scenarios`): free-play configurations at
the top level, lesson chapters in `game/scenarios/chapters`. Parsing is strict everywhere:
unknown keys, wrong types, fractional numbers and out-of-range values are errors with a path to
the problem, so typos never pass silently.

- The **extension** (`gdext/src/scenario_config.cpp`) reads the top level and the
  `simulation`/`runtime` sections, via `Simulation.load_scenario(path)` or `configure(dict)`.
  It accepts versions 1 and 2 and, in version 2, only checks that `lesson` is an object.
- The **game layer** (`game/lessons/chapter_loader.gd`) reads `lesson` into a `ChapterDef`.

## Top level and simulation configuration

```json
{
    "format": "stocksim-scenario",
    "format_version": 1,
    "name": "Sandbox",
    "description": "Free text.",
    "simulation": {
        "snapshot_interval_ticks": 64,
        "expected_orders": 1024,
        "expected_events": 4096
    },
    "runtime": {
        "event_buffer_capacity": 65536
    }
}
```

| Key | Type | Required | Default | Allowed | Meaning |
| --- | ---- | -------- | ------- | ------- | ------- |
| `format` | string | yes | | `"stocksim-scenario"` | Identifies the file type |
| `format_version` | integer | yes | | `1` or `2` | Format version; `lesson` needs `2` |
| `name` | string | no | `"Default"` | | Shown in the UI |
| `description` | string | no | | | Free text |
| `simulation.snapshot_interval_ticks` | integer | no | 64 | 0 to 1000000 | Ticks between state snapshots; bounds the cost of `step_back`. 0 keeps only the initial snapshot |
| `simulation.expected_orders` | integer | no | 1024 | 0 to 2^24 | Capacity hint for live orders |
| `simulation.expected_events` | integer | no | 4096 | 0 to 2^24 | Capacity hint for the core event buffer |
| `runtime.event_buffer_capacity` | integer | no | 65536 | 64 to 2^22 | Events that can wait between the simulation and `drain_events()` (rounded up to a power of two). When full, running pauses until drained |
| `lesson` | object | no (v2 only) | | | A lesson chapter, below |

## Lessons (version 2)

A chapter is a list of **beats**. Each beat shows text, runs its actions (scripted inputs to
the simulation, or UI instructions), and waits before the learner can continue. All names are
fictional. Prices are decimal **strings** (`"100.25"`), converted to ticks exactly; quantities
and accounts are integers.

```json
"lesson": {
    "chapter": 2,
    "title": "The first trade",
    "summary": "One or two sentences for the chapter list.",
    "features": ["chart", "ladder", "tape", "narration", "inspector", "ticket"],
    "learner_account": 10,
    "participants": [{"account": 2, "name": "Ledger Lane Markets", "role": "Market maker"}],
    "setup": [
        {"do": "submit", "account": 2, "side": "sell", "type": "limit", "qty": 100, "price": "100.10"},
        {"do": "tick"}
    ],
    "beats": [
        {"id": "recap", "title": "A market with no price", "text": "BBCode text."},
        {"id": "learner_buys", "text": "Buy 10 at market.",
         "wait": {"kind": "learner_order", "hint": "Use the order ticket.",
                  "suggested": {"side": "buy", "type": "market", "qty": 10, "tag": "first"}}},
        {"id": "process", "text": "Step until it trades.",
         "wait": {"kind": "event", "event": "match", "tag": "first", "hint": "Press Step."}}
    ]
}
```

| Key | Type | Required | Meaning |
| --- | ---- | -------- | ------- |
| `chapter` | integer 1-999 | yes | Order in the chapter list; numbered without gaps |
| `title`, `summary` | string | yes | Shown in the chapter list and lesson panel |
| `features` | strings | no | Panels shown: `chart`, `ladder`, `tape`, `narration`, `inspector`, `ticket`, `seed`, `run`. Others are hidden |
| `learner_account` | integer | yes | The account the learner trades as (shown as "You") |
| `participants` | objects | no | `account`, `name` (required), `role`: names shown in narration |
| `setup` | actions | no | Run before the first beat, to build the starting market |
| `beats` | objects | yes | At least one; `id` (unique), `text` (BBCode) required; `title`, `actions`, `wait` optional |

### Actions (`"do"`)

| `do` | Fields | Effect |
| ---- | ------ | ------ |
| `submit` | `account`, `side` (`buy`/`sell`), `type` (`market`/`limit`/`stop`/`stop_limit`), `qty`, `price` (limit types), `stop` (stop types), `tag` | Queues one order; `tag` remembers its id |
| `ladder` | `account`, `side`, `from`, `step`, `levels` (1-200), `qty`, `qty_step` | Limit orders from `from`, `step` apart (asks upward, bids downward) |
| `tick` / `step` | `count` (default 1) | Runs ticks / single steps |
| `reset` | | Fresh market from the same scenario (records are kept) |
| `highlight` | exactly one of `price`, `account`, `tag` | Selects it in the UI |
| `record` | `tag`, `label` | Records how the tagged order filled |
| `show_comparison` | | Shows all records side by side |

### Waits (`"wait"`, default `{"kind": "next"}`)

| `kind` | Fields | Satisfied when |
| ------ | ------ | -------------- |
| `next` | | Immediately: the learner clicks Next |
| `event` | `event` (`order_placed`, `order_rejected`, `order_rested`, `order_cancelled`, `order_modified`, `command_rejected`, `stop_triggered`, `match`, `fill`, `price_change`), optional `tag`, `hint` | Such an event happens (for the tagged order); the learner uses Step/Tick/Run |
| `learner_order` | `suggested` (an order without `account`), `hint` | The learner places any order; "Do it for me" places `suggested`, whose `tag` names the learner's order |

Every tag used by `highlight`, `record` or a wait must be set by an earlier `submit` or
suggestion.

## Changing the format

Bump `format_version`, update this document, the parsers (`gdext/src/scenario_config.cpp`,
`game/lessons/chapter_loader.gd`), their tests, and the files in `game/scenarios`.
