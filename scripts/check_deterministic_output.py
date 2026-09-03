#!/usr/bin/env python3
"""Compare two generated trees byte-for-byte with stable diagnostics."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree(root: Path) -> dict[str, str]:
    return {
        str(path.relative_to(root)): digest(path)
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    arguments = parser.parse_args()
    first = tree(arguments.first)
    second = tree(arguments.second)
    if first == second:
        print("generated trees are byte-identical")
        return 0
    for name in sorted(set(first) | set(second)):
        if first.get(name) != second.get(name):
            print(f"{name}: {first.get(name, 'missing')} != {second.get(name, 'missing')}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
