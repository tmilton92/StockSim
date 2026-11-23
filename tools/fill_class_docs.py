#!/usr/bin/env python3
"""Fill descriptions into a Godot class reference XML skeleton.

Godot generates the skeleton from the loaded GDExtension
(`godot --headless --path game --doctool <dir> --gdextension-docs`), which keeps the XML exactly
in sync with the bindings. This script merges hand-written descriptions from a JSON file into
it, and fails if any method, signal or constant is left undocumented or a description refers to
something that no longer exists.

Usage:
    python tools/fill_class_docs.py SKELETON.xml DESCRIPTIONS.json OUTPUT.xml
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def indent_block(text: str, tabs: int) -> str:
    pad = "\t" * tabs
    return "\n".join(pad + line if line else "" for line in text.strip().split("\n"))


def fill(skeleton: str, docs: dict) -> tuple[str, list[str]]:
    problems: list[str] = []
    used: set[str] = set()

    def replace_top(tag: str, key: str) -> None:
        nonlocal skeleton
        pattern = re.compile(rf"<{tag}>\n\t</{tag}>")
        if not pattern.search(skeleton):
            problems.append(f"skeleton has no empty <{tag}>")
            return
        body = indent_block(docs[key], 2)
        skeleton = pattern.sub(lambda _: f"<{tag}>\n{body}\n\t</{tag}>", skeleton, count=1)

    replace_top("brief_description", "brief_description")
    replace_top("description", "description")

    def fill_members(kind: str, section: str, tabs: int) -> None:
        nonlocal skeleton
        entries: dict[str, str] = docs.get(section, {})
        # Matches <method name="x" ...> ... <description>\n\t\t\t</description>
        pattern = re.compile(
            rf'(<{kind} name="(?P<name>[A-Za-z0-9_]+)"[^>]*>(?:(?!</{kind}>).)*?<description>)'
            rf"\n\t*(</description>)",
            re.S,
        )

        def substitute(match: re.Match[str]) -> str:
            name = match.group("name")
            if name not in entries:
                problems.append(f"undocumented {kind}: {name}")
                return match.group(0)
            used.add(f"{section}:{name}")
            body = indent_block(entries[name], tabs + 1)
            return f"{match.group(1)}\n{body}\n{chr(9) * tabs}{match.group(3)}"

        skeleton = pattern.sub(substitute, skeleton)
        for name in entries:
            if f"{section}:{name}" not in used:
                problems.append(f"documented {kind} not in bindings: {name}")

    fill_members("method", "methods", 3)
    fill_members("signal", "signals", 3)

    constants: dict[str, str] = docs.get("constants", {})
    constant_pattern = re.compile(
        r'(<constant name="(?P<name>[A-Z0-9_]+)"[^>]*>)\n\t*(</constant>)'
    )

    def substitute_constant(match: re.Match[str]) -> str:
        name = match.group("name")
        if name not in constants:
            problems.append(f"undocumented constant: {name}")
            return match.group(0)
        used.add(f"constants:{name}")
        return f"{match.group(1)}\n{indent_block(constants[name], 3)}\n\t\t{match.group(3)}"

    skeleton = constant_pattern.sub(substitute_constant, skeleton)
    for name in constants:
        if f"constants:{name}" not in used:
            problems.append(f"documented constant not in bindings: {name}")
    return skeleton, problems


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    skeleton = Path(sys.argv[1]).read_text(encoding="utf-8")
    docs = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
    output, problems = fill(skeleton, docs)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    Path(sys.argv[3]).write_text(output, encoding="utf-8", newline="\n")
    print(f"wrote {sys.argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
