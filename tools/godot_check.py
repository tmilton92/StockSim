#!/usr/bin/env python3
"""Headless Godot checks for the game project and the stocksim GDExtension.

Usage:
    python tools/godot_check.py --godot PATH_TO_GODOT_BINARY

Steps (all must pass):
  1. Import the project: no errors or warnings.
  2. The GDExtension loads.
  3. The class reference XML in gdext/doc_classes matches the bindings: Godot regenerates the
     skeleton from the loaded extension and tools/fill_class_docs.py fills it from the
     *.docs.json descriptions; the result must equal the committed XML.
  4. The Simulation smoke test (game/tests/smoke) passes. It announces each deliberately
     triggered engine error with "EXPECT_ERROR: <text>"; every logged error must match one
     announcement and every announcement must be matched. Warnings are never allowed.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = REPO_ROOT / "game"
DOC_DIR = REPO_ROOT / "gdext" / "doc_classes"
SANDBOX_SCENARIO = REPO_ROOT / "scenarios" / "sandbox.json"
SMOKE_SCRIPT = "res://tests/smoke/simulation_smoke.gd"
EXTENSION = "res://bin/stocksim.gdextension"
ANSI = re.compile(r"\x1b\[[0-9;]*m")
ERROR_START = re.compile(r"^\s*(ERROR|SCRIPT ERROR|USER ERROR):")
WARNING_START = re.compile(r"^\s*(WARNING|USER WARNING|SCRIPT WARNING):")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fill_class_docs import fill  # noqa: E402  (sibling tool, imported after path setup)

PROBE_SCRIPT = """extends SceneTree


func _init() -> void:
	var loaded: PackedStringArray = GDExtensionManager.get_loaded_extensions()
	print("STOCKSIM_LOADED_EXTENSIONS=", ",".join(loaded))
	quit()
"""


@dataclass
class Run:
    output: str
    returncode: int


class CheckFailed(Exception):
    pass


def godot(binary: str, args: list[str]) -> Run:
    result = subprocess.run(
        [binary, "--headless", "--path", str(GAME_DIR), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
    )
    output = ANSI.sub("", result.stdout + result.stderr)
    print(output)
    return Run(output, result.returncode)


def error_blocks(output: str) -> list[str]:
    """Each engine error with its continuation lines (e.g. '   at: file:line')."""
    blocks: list[str] = []
    for line in output.splitlines():
        if ERROR_START.match(line):
            blocks.append(line)
        elif blocks and line.startswith((" ", "\t")) and line.strip():
            blocks[-1] += "\n" + line
    return blocks


def require_clean(run: Run, what: str) -> None:
    if run.returncode != 0:
        raise CheckFailed(f"{what} exited with {run.returncode}")
    problems = error_blocks(run.output) + [
        line for line in run.output.splitlines() if WARNING_START.match(line)
    ]
    if problems:
        raise CheckFailed(f"{what} reported errors/warnings:\n" + "\n".join(problems))


def seed_extension_list() -> None:
    """Workaround for a Godot 4.7.x engine bug (see docs/VERSIONS.md): when a fresh project
    discovers a GDExtension during its first scan, the editor crashes at exit (0xC0000005 /
    SIGSEGV). Listing the extensions up front makes Godot load them at startup instead."""
    cache = GAME_DIR / ".godot"
    extension_list = cache / "extension_list.cfg"
    if extension_list.is_file():
        return
    extensions = sorted(
        "res://" + path.relative_to(GAME_DIR).as_posix()
        for path in GAME_DIR.rglob("*.gdextension")
        if ".godot" not in path.parts
    )
    cache.mkdir(exist_ok=True)
    extension_list.write_text("".join(f"{ext}\n" for ext in extensions), encoding="utf-8")


def check_import(binary: str) -> None:
    seed_extension_list()
    require_clean(godot(binary, ["--import"]), "project import")


def check_extension_loads(binary: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe_extensions.gd"
        probe.write_text(PROBE_SCRIPT, encoding="utf-8")
        run = godot(binary, ["--script", str(probe)])
    require_clean(run, "extension load probe")
    match = re.search(r"STOCKSIM_LOADED_EXTENSIONS=(.*)", run.output)
    loaded = match.group(1).strip().split(",") if match else []
    if EXTENSION not in loaded:
        raise CheckFailed(f"{EXTENSION} was not loaded (loaded: {loaded})")


def check_class_docs(binary: str) -> None:
    import json

    with tempfile.TemporaryDirectory() as tmp:
        run = godot(binary, ["--doctool", tmp, "--gdextension-docs"])
        require_clean(run, "doctool")
        generated = sorted((Path(tmp) / "doc_classes").glob("*.xml"))
        if not generated:
            raise CheckFailed("doctool generated no class reference files")
        for skeleton_path in generated:
            committed = DOC_DIR / skeleton_path.name
            descriptions = DOC_DIR / (skeleton_path.stem + ".docs.json")
            if not descriptions.is_file():
                raise CheckFailed(f"missing descriptions file {descriptions}")
            filled, problems = fill(
                skeleton_path.read_text(encoding="utf-8"),
                json.loads(descriptions.read_text(encoding="utf-8")),
            )
            if problems:
                raise CheckFailed(f"{skeleton_path.name}: " + "; ".join(problems))
            if not committed.is_file() or committed.read_text(encoding="utf-8") != filled:
                raise CheckFailed(
                    f"{committed} is out of date with the bindings; regenerate it "
                    "(see docs/BUILDING.md)"
                )


def check_smoke_test(binary: str) -> None:
    run = godot(binary, ["--script", SMOKE_SCRIPT, "--", str(SANDBOX_SCENARIO)])
    expected = [m.group(1) for m in re.finditer(r"^EXPECT_ERROR: (.*)$", run.output, re.M)]
    unmatched_errors: list[str] = []
    for block in error_blocks(run.output):
        match = next((text for text in expected if text in block), None)
        if match is None:
            unmatched_errors.append(block)
        else:
            expected.remove(match)
    warnings = [line for line in run.output.splitlines() if WARNING_START.match(line)]
    problems: list[str] = []
    if run.returncode != 0:
        problems.append(f"smoke test exited with {run.returncode} (number of failed checks)")
    if not re.search(r"^SMOKE: \d+ checks, 0 failures$", run.output, re.M):
        problems.append("smoke test did not report a clean summary")
    problems += [f"unexpected error:\n{block}" for block in unmatched_errors]
    problems += [f"expected error did not occur: {text}" for text in expected]
    problems += [f"warning: {line}" for line in warnings]
    if problems:
        raise CheckFailed("\n".join(problems))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, help="Godot 4.7.2 editor binary")
    args = parser.parse_args()
    steps = [
        ("import", check_import),
        ("extension load", check_extension_loads),
        ("class docs", check_class_docs),
        ("smoke test", check_smoke_test),
    ]
    for name, step in steps:
        try:
            step(args.godot)
        except CheckFailed as failure:
            print(f"FAILED ({name}): {failure}", file=sys.stderr)
            return 1
        print(f"OK: {name}")
    print("OK: all Godot checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
