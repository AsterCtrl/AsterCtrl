#!/usr/bin/env python3
"""Guard the prerelease workflow against publishing a final release."""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path

ALPHA_TAG = re.compile(r"v0\.2\.0-alpha\.[1-9][0-9]*")
SOURCE_VERSION = re.compile(r'kVersion\s*=\s*"([^"]+)"')


def _match_version(root: Path, relative: str, pattern: str) -> str:
    path = root / relative
    match = re.search(pattern, path.read_text(encoding="utf-8"), re.MULTILINE)
    if match is None:
        raise SystemExit(f"cannot read a version from {path}")
    return match.group(1)


def _pep440_version(version: str) -> str:
    match = re.fullmatch(r"(\d+\.\d+\.\d+)-alpha\.([1-9][0-9]*)", version)
    if match is None:
        raise SystemExit(f"cannot map source version {version!r} to a Python alpha version")
    return f"{match.group(1)}a{match.group(2)}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tag", nargs="?", default=os.environ.get("GITHUB_REF_NAME", ""))
    args = parser.parse_args()
    if ALPHA_TAG.fullmatch(args.tag) is None:
        raise SystemExit(f"refusing release for {args.tag!r}; only v0.2.0-alpha.N is allowed")
    root = Path(__file__).resolve().parents[2]
    version_header = root / "include/aster/version.hpp"
    match = SOURCE_VERSION.search(version_header.read_text(encoding="utf-8"))
    if match is None:
        raise SystemExit(f"cannot read the source version from {version_header}")
    expected_tag = f"v{match.group(1)}"
    if args.tag != expected_tag:
        raise SystemExit(
            f"refusing release for {args.tag!r}; source version requires {expected_tag!r}"
        )

    semver_sources = {
        "transport header": _match_version(
            root, "include/aster/transport/version.hpp", SOURCE_VERSION.pattern
        ),
        "CMake package": _match_version(
            root, "CMakeLists.txt", r'set\(ASTERCTRL_VERSION_STRING\s+"([^"]+)"\)'
        ),
        "core package manifest": _match_version(root, "package.yaml", r"^\s*version:\s*([^\s#]+)"),
        "CLI package manifest": _match_version(
            root, "tools/aster_cli/package.yaml", r"^\s*version:\s*([^\s#]+)"
        ),
        "CLI runtime": _match_version(
            root, "tools/aster_cli/src/aster_cli/__init__.py", r'__version__\s*=\s*"([^"]+)"'
        ),
        "Sphinx documentation": _match_version(
            root, "document/conf.py", r'^release\s*=\s*"([^"]+)"'
        ),
    }
    mismatches = {
        source: version for source, version in semver_sources.items() if version != match.group(1)
    }
    if mismatches:
        details = ", ".join(f"{source}={version!r}" for source, version in mismatches.items())
        raise SystemExit(f"release versions disagree with {match.group(1)!r}: {details}")

    expected_python = _pep440_version(match.group(1))
    for relative in ("pyproject.toml", "tools/aster_cli/pyproject.toml"):
        path = root / relative
        declared = _match_version(
            root,
            relative,
            r'^\[project\]\s*$[\s\S]*?^version\s*=\s*"([^"]+)"',
        )
        if declared != expected_python:
            raise SystemExit(
                f"Python project {path} declares {declared!r}; expected {expected_python!r}"
            )
    print(f"verified prerelease tag {args.tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
