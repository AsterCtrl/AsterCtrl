#!/usr/bin/env python3
"""Reject platform and legacy dependencies in portable AsterCtrl sources."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FORBIDDEN = re.compile(
    r"(?:#\s*include\s*[<\"](?:zephyr|posix|rclcpp|aimrt|libxr|xrobot|FreeRTOS|stm32))"
    r"|(?:\b(?:LibXR|XRobot|FreeRTOS)\b)",
    re.IGNORECASE,
)
SUFFIXES = {".h", ".hh", ".hpp", ".c", ".cc", ".cpp", ".cxx"}


def scan(root: Path) -> list[str]:
    violations: list[str] = []
    portable_roots = (root / "include" / "aster", root / "src" / "core")
    excluded_parts = {"platform", "transport", "transports"}
    for source_root in portable_roots:
        if not source_root.exists():
            continue
        for path in sorted(source_root.rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            relative = path.relative_to(root)
            if excluded_parts.intersection(relative.parts):
                continue
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), start=1
            ):
                if FORBIDDEN.search(line):
                    violations.append(f"{relative}:{line_number}: {line.strip()}")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    arguments = parser.parse_args()
    violations = scan(arguments.root.resolve())
    if violations:
        print("portable source contains forbidden dependencies:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    print("portable dependency check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
