#!/usr/bin/env python3
"""Run clang-tidy over all project translation units (core, core tests, gdext).

Usage:
    python tools/run_clang_tidy.py [--build-dir build/debug] [--clang-tidy PATH]

Requires a configured build directory containing compile_commands.json (every CMake preset
exports one). Third-party code is excluded by HeaderFilterRegex in .clang-tidy.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROJECT_DIRS = ("core", "gdext/src")

# Extra arguments needed when the compile database comes from MSVC:
# - the UCRT offsetof macro is not a constant expression under clang unless the builtin is used;
# - godot-cpp adds /MP, which clang-cl reports as unused.
MSVC_EXTRA_ARGS = ("-D_CRT_USE_BUILTIN_OFFSETOF", "-Wno-unused-command-line-argument")


def project_sources(build_dir: Path) -> tuple[list[str], bool]:
    database = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    roots = [(REPO_ROOT / d).resolve() for d in PROJECT_DIRS]
    files: set[str] = set()
    msvc = False
    for entry in database:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        path = path.resolve()
        if any(path.is_relative_to(root) for root in roots):
            files.add(str(path))
            command = entry.get("command") or " ".join(entry.get("arguments", []))
            msvc = msvc or "cl.exe" in command.lower()
    return sorted(files), msvc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build/debug", type=Path)
    parser.add_argument("--clang-tidy", default="clang-tidy", help="clang-tidy binary")
    args = parser.parse_args()

    build_dir = args.build_dir if args.build_dir.is_absolute() else REPO_ROOT / args.build_dir
    if not (build_dir / "compile_commands.json").is_file():
        print(f"{build_dir}/compile_commands.json not found; configure a preset first",
              file=sys.stderr)
        return 1

    files, msvc = project_sources(build_dir)
    if not files:
        print("no project sources in the compile database", file=sys.stderr)
        return 1

    binary = shutil.which(args.clang_tidy) or args.clang_tidy
    command = [binary, "-p", str(build_dir), "--quiet"]
    if msvc:
        command.extend(f"--extra-arg={arg}" for arg in MSVC_EXTRA_ARGS)
    result = subprocess.run([*command, *files], cwd=REPO_ROOT, check=False)
    if result.returncode == 0:
        print(f"clang-tidy clean on {len(files)} translation unit(s)")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
