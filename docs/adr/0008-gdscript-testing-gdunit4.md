# ADR 0008: gdUnit4 for GDScript tests, vendored addon, strict script check

- Status: Accepted
- Date: 2026-11-23

## Context

CLAUDE.md requires GDScript tests for UI logic and the lesson runner using gdUnit4 or GUT,
chosen once. Section 4 adds the first real game-layer code. Godot loads addons from
`res://addons`, and both candidates ship as Godot projects, so they cannot be used directly
from a git submodule (ADR 0003 covers C++ dependencies only).

## Decision

### Framework

- gdUnit4 **v6.2.3** (`f2e4f8679ebd28e27ef523a4fb7b09e8337bfce9`), MIT. Its compatibility table
  lists Godot 4.7 and 4.7.1 for v6.2.x; verified here against 4.7.2-stable by running the suites.
- Suites live in `game/tests/unit/<area>/test_*.gd` and extend `GdUnitTestSuite`. UI tests use
  `scene_runner()` with explicit frame deltas, so animations are deterministic.
- The editor plugin is not enabled: the CLI runner does not need it, and the project stays free
  of editor-only state. Enable it locally from Project Settings > Plugins if wanted.

### Vendoring

- `tools/vendor_gdunit4.py` clones the pinned tag, verifies the exact commit, and copies
  `addons/gdUnit4` into `game/addons/gdUnit4` (committed, with `LICENSE` and `VENDORED.txt`).
  Files in that directory are never edited by hand; upgrades re-run the script.
- Excluded from the copy: the addon's own self-tests, and AI-assistant instruction files shipped
  upstream (`CLAUDE.md`, `AGENTS.md`, `llms*.txt`), which would otherwise be read as
  instructions for this repository.
- Addon scripts are excluded from our warning rules by Godot's default
  `debug/gdscript/warnings/directory_rules` (`res://addons` = exclude).

### Running

- `tools/godot_check.py` runs the suites headless:
  `godot --headless --path game -s res://addons/gdUnit4/bin/GdUnitCmdTool.gd
  --ignoreHeadlessMode -c -a res://tests/unit -rd <temp dir>`. Exit codes: 0 pass, 100 test
  failures, 101 warnings, 103 headless refused, 104 unsupported Godot, 105 script errors. Any
  non-zero exit, error line or warning line fails the check. Verified to fail on a deliberately
  failing test.
- The Section 3 smoke script stays: it covers the extension's API surface and error paths.

### Strict script check

`--check-only` reports only warnings configured as errors, and cannot resolve autoload names.
`godot_check.py` therefore also writes a temporary `game/override.cfg` raising every
Warn-level GDScript warning to Error, and loads every project script inside a normal script run,
where autoload names resolve. This found real shadowing warnings that the other checks missed.

## Consequences

- About 3.7 MB of third-party GDScript is committed under `game/addons/gdUnit4`.
- Tests that need autoloads (e.g. `SimBridge`) work under the gdUnit4 runner; scripts run
  directly with `--script` cannot name autoloads, so visual checks run as scenes
  (`game/tests/visual`).
- Upgrading gdUnit4 or Godot means re-running the vendor script and the full check suite.
