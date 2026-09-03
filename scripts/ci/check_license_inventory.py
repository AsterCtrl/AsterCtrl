#!/usr/bin/env python3
"""Reject known copyleft dependencies from a pip-licenses JSON inventory."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

DENIED = ("AGPL", "Affero", "GPL-2", "GPL-3", "GNU General Public License")
PERMISSIVE = (
    "Apache",
    "BSD",
    "ISC",
    "MIT",
    "Public Domain",
    "Python Software Foundation",
    "Unlicense",
    "Zlib",
)


def denied_without_alternative(license_name: str) -> bool:
    normalized = license_name.lower()
    has_denied = any(token.lower() in normalized for token in DENIED)
    has_permissive_alternative = any(token.lower() in normalized for token in PERMISSIVE)
    return has_denied and not has_permissive_alternative


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inventory", type=Path)
    args = parser.parse_args()
    packages = json.loads(args.inventory.read_text(encoding="utf-8"))
    denied = [
        f"{package['Name']} {package['Version']}: {package['License']}"
        for package in packages
        if denied_without_alternative(package.get("License", ""))
    ]
    unknown = [
        f"{package['Name']} {package['Version']}"
        for package in packages
        if package.get("License", "").upper() == "UNKNOWN"
    ]
    if unknown:
        print("license metadata unavailable (dependency review remains authoritative):")
        print("\n".join(f"- {package}" for package in unknown))
    if denied:
        print("denied dependency licenses:")
        print("\n".join(f"- {package}" for package in denied))
        return 1
    print("known dependency licenses satisfy policy")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
