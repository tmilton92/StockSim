# Scenario format (version 1)

Scenarios are JSON files in `/scenarios`, loaded with `Simulation.load_scenario(path)` or, once
parsed, `Simulation.configure(dictionary)`. Parsing is strict: unknown keys, wrong types,
fractional numbers and out-of-range values are errors, so typos never pass silently.

Version 1 covers simulation configuration only. Lesson content (initial state, enabled
features, narration cues, pause points) is added with the lesson system (TODO §5), as a new
format version.

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
| `format_version` | integer | yes | | `1` | Format version |
| `name` | string | no | `"Default"` | | Shown in the UI |
| `description` | string | no | | | Free text |
| `simulation.snapshot_interval_ticks` | integer | no | 64 | 0 to 1000000 | Ticks between state snapshots; bounds the cost of `step_back`. 0 keeps only the initial snapshot |
| `simulation.expected_orders` | integer | no | 1024 | 0 to 2^24 | Capacity hint for live orders |
| `simulation.expected_events` | integer | no | 4096 | 0 to 2^24 | Capacity hint for the core event buffer |
| `runtime.event_buffer_capacity` | integer | no | 65536 | 64 to 2^22 | Events that can wait between the simulation and `drain_events()` (rounded up to a power of two). When full, running pauses until drained |

Changing this format means bumping `format_version`, updating this document, the parser in
`gdext/src/scenario_config.cpp`, and the scenario files in `/scenarios`.
