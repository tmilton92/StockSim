#!/usr/bin/env python3
"""Vendor the gdUnit4 addon into game/addons/gdUnit4 at a pinned, verified commit.

Godot loads addons from res://addons, so gdUnit4 cannot be used straight from a submodule
(its repository root is itself a Godot project). This script clones the pinned tag into a
temporary directory, verifies the exact commit, and copies `addons/gdUnit4` (minus the
addon's own self-tests) plus its license into the game project. See docs/adr/0008.

Usage:
    python tools/vendor_gdunit4.py

To upgrade: change TAG and COMMIT (and docs/VERSIONS.md), run this, run all checks.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = "https://github.com/godot-gdunit-labs/gdUnit4"
TAG = "v6.2.3"
COMMIT = "f2e4f8679ebd28e27ef523a4fb7b09e8337bfce9"

REPO_ROOT = Path(__file__).resolve().parent.parent
DESTINATION = REPO_ROOT / "game" / "addons" / "gdUnit4"
# The addon's own self-tests, and AI-assistant instruction files shipped upstream (they would
# otherwise be picked up as instructions for this repository).
EXCLUDED = ("test", "CLAUDE.md", "AGENTS.md", "llms.txt", "llms-full.txt", ".cursorrules")


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        checkout = Path(tmp) / "gdUnit4"
        subprocess.run(
            ["git", "clone", "--quiet", "--depth", "1", "--branch", TAG, REPOSITORY,
             str(checkout)],
            check=True,
        )
        head = subprocess.run(
            ["git", "-C", str(checkout), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        if head != COMMIT:
            print(f"tag {TAG} resolved to {head}, expected {COMMIT}; refusing to vendor",
                  file=sys.stderr)
            return 1

        source = checkout / "addons" / "gdUnit4"
        if DESTINATION.exists():
            shutil.rmtree(DESTINATION)
        shutil.copytree(source, DESTINATION, ignore=shutil.ignore_patterns(*EXCLUDED))
        shutil.copy2(checkout / "LICENSE", DESTINATION / "LICENSE")
        (DESTINATION / "VENDORED.txt").write_text(
            f"Vendored by tools/vendor_gdunit4.py\nrepository: {REPOSITORY}\n"
            f"tag: {TAG}\ncommit: {COMMIT}\nexcluded: {', '.join(EXCLUDED)}\n"
            "Do not edit files in this directory; re-run the script instead.\n",
            encoding="utf-8",
        )
    files = sum(1 for path in DESTINATION.rglob("*") if path.is_file())
    print(f"vendored gdUnit4 {TAG} ({COMMIT[:12]}): {files} files in {DESTINATION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
