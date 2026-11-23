#!/usr/bin/env python3
"""Headless Godot smoke check: import the project and verify the GDExtension loads.

Usage:
    python tools/godot_check.py --godot PATH_TO_GODOT_BINARY

Fails if the import or the load probe exits non-zero, if any ERROR/WARNING lines appear in the
engine output, or if res://bin/stocksim.gdextension is not among the loaded extensions.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = REPO_ROOT / "game"
EXTENSION = "res://bin/stocksim.gdextension"
PROBLEM = re.compile(r"^\s*(ERROR|WARNING|SCRIPT ERROR)\b", re.MULTILINE)
ANSI = re.compile(r"\x1b\[[0-9;]*m")

PROBE_SCRIPT = """extends SceneTree


func _init() -> void:
	var loaded: PackedStringArray = GDExtensionManager.get_loaded_extensions()
	print("STOCKSIM_LOADED_EXTENSIONS=", ",".join(loaded))
	quit()
"""


def run(godot: str, args: list[str]) -> str:
    result = subprocess.run(
        [godot, "--headless", "--path", str(GAME_DIR), *args],
        capture_output=True, text=True, encoding="utf-8", errors="replace", check=False,
    )
    output = ANSI.sub("", result.stdout + result.stderr)
    print(output)
    if result.returncode != 0:
        raise SystemExit(f"godot {' '.join(args)} exited with {result.returncode}")
    problems = PROBLEM.findall(output)
    if problems:
        raise SystemExit(f"godot {' '.join(args)} reported {len(problems)} error/warning line(s)")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, help="Godot 4.7.2 editor binary")
    args = parser.parse_args()

    run(args.godot, ["--import"])

    # The probe lives outside the project so it never ends up in res:// or the import cache.
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe_extensions.gd"
        probe.write_text(PROBE_SCRIPT, encoding="utf-8")
        output = run(args.godot, ["--script", str(probe)])

    match = re.search(r"STOCKSIM_LOADED_EXTENSIONS=(.*)", output)
    loaded = match.group(1).strip().split(",") if match else []
    if EXTENSION not in loaded:
        print(f"{EXTENSION} was not loaded (loaded: {loaded})", file=sys.stderr)
        return 1
    print(f"OK: project imports cleanly and {EXTENSION} loads")
    return 0


if __name__ == "__main__":
    sys.exit(main())
