#!/usr/bin/env python3
"""Check (or fix) clang-format compliance of all tracked C++ sources in /core and /gdext.

Usage:
    python tools/check_format.py [--fix] [--clang-format PATH]

Exits non-zero if any file needs formatting (check mode) or clang-format fails.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("core", "gdext/src")
# CMake templates (*.in) are not valid C++ until configured, so they are not formatted.
EXTENSIONS = {".cpp", ".hpp", ".h"}


def find_sources() -> list[Path]:
    files: list[Path] = []
    for rel in SOURCE_DIRS:
        root = REPO_ROOT / rel
        files.extend(p for p in root.rglob("*") if p.is_file() and p.suffix in EXTENSIONS)
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true", help="rewrite files in place")
    parser.add_argument("--clang-format", default="clang-format", help="clang-format binary")
    args = parser.parse_args()

    binary = shutil.which(args.clang_format) or args.clang_format
    files = find_sources()
    if not files:
        print("no sources found", file=sys.stderr)
        return 1

    version = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True)
    print(version.stdout.strip())

    mode = ["-i"] if args.fix else ["--dry-run", "--Werror"]
    result = subprocess.run([binary, *mode, *map(str, files)], cwd=REPO_ROOT, check=False)
    if result.returncode == 0:
        print(f"{len(files)} file(s) {'formatted' if args.fix else 'clean'}")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
