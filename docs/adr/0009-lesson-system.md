# ADR 0009: Lesson system, scenario format v2, and scenario location

- Status: Accepted
- Date: 2026-11-23

## Context

TODO §5 adds chapters: a scenario format with initial state, enabled features, narration and
pause points; a chapter loader and select menu; and chapters 1-3. Decisions taken with the user:

- lessons are defined in JSON (scenario format v2), not Godot resources;
- scenario files move into the Godot project so exported builds can read them;
- lessons advance with a Next button, plus beats that wait for the learner to act;
- chapter 3 compares a thin and a deep book sequentially, with a summary table.

## Decision

### Format and parsing

- Scenario `format_version` 2 adds an optional `lesson` object (docs/scenario-format.md). The
  extension accepts versions 1-2 and only checks that `lesson` is an object; the simulation
  config is unchanged.
- `ChapterLoader` (GDScript) parses `lesson` strictly with `JsonReader`: unknown keys, wrong
  types, fractions, unknown enum strings and tags used before they are set are all errors,
  reported together with paths. Prices are decimal strings parsed exactly to ticks.
- The result is a `ChapterDef` Resource tree (`BeatDef`, `LessonAction`, `LessonWait`,
  `ParticipantDef`), per CLAUDE.md's "data lives in Resources".

### Location

- `/scenarios` moved to `game/scenarios` (`res://scenarios`), so the files ship with exported
  builds and need no copy step; chapters live in `game/scenarios/chapters`. CLAUDE.md's layout
  was updated. (ADR 0007 still mentions `/scenarios`; this ADR supersedes that detail.)

### Running lessons

- `LessonRunner` (a node owned by `LessonScreen`, not an autoload) loads the chapter's scenario
  through `SimBridge`, runs `setup`, then the beats. Actions are ordinary simulation inputs
  (orders, steps, ticks, resets) or UI requests (highlight, comparison). It never decides
  market outcomes: chapter results are whatever the core produces.
- Waits: `next`; `event` (an event of a type, optionally for a tagged order, checked against
  events since the beat began so a beat's own actions can satisfy it); `learner_order` (any
  order from the ticket, or "Do it for me" placing the suggestion; the suggestion's tag names
  the learner's order).
- `ImpactRecord` summarises a tagged order's fills: exact integer notional and quantity, best
  quote at submission, worst fill, levels used, price move. Averages are computed only as text.
- `App` (main scene) swaps between `ChapterSelect`, `LessonScreen` (lesson panel + market
  screen) and the sandbox. `MainScreen` gained `apply_features`, `set_account_names`,
  `set_learner_account`, and `learner_order_submitted` / `menu_requested` signals.

### Tests

- Every chapter file loads, is numbered in order, and plays headless to completion (the test acts
  as the learner). Chapter tests assert the teaching points against core output, e.g. chapter 3's
  thin book uses 5 levels and moves the price 1.90 while the deep book uses 1 and moves 0.
- UI tests drive the app shell and lesson panel through their own buttons.

## Consequences

- New chapters are data-only unless they need a new action or wait kind (then the loader, the
  runner, the format document and tests change together, with a format version bump if old
  files would be read differently).
- Lessons depend on core behaviour; a change to matching that alters a chapter's outcome fails
  that chapter's test, which is intended.
