#!/usr/bin/env python3
"""Enforce reproducible dependency references used by builds and CI."""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from pathlib import Path

import yaml

COMMIT = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"sha256:[0-9a-f]{64}")
ACTION = re.compile(r"^\s*uses:\s*([^@\s]+)@([^\s#]+)", re.MULTILINE)
FETCH_CONTENT = re.compile(r"FetchContent_Declare\s*\([^)]*\)", re.IGNORECASE | re.DOTALL)


def check_west(root: Path, failures: list[str]) -> None:
    manifest_path = root / "west.yml"
    manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))["manifest"]
    for project in manifest.get("projects", []):
        name = project.get("name", "<unnamed>")
        revision = str(project.get("revision", ""))
        if COMMIT.fullmatch(revision) is None:
            failures.append(f"west.yml project {name} uses mutable revision {revision!r}")


def check_uv_lock(path: Path, failures: list[str]) -> None:
    if not path.is_file():
        failures.append(f"missing lock file: {path}")
        return
    with path.open("rb") as stream:
        lock = tomllib.load(stream)
    for package in lock.get("package", []):
        source = package.get("source", {})
        name = package.get("name", "<unnamed>")
        if "git" in source:
            revision = str(source.get("rev", ""))
            if COMMIT.fullmatch(revision) is None:
                failures.append(f"{path}: git package {name} is not commit-pinned")
        if "registry" not in source:
            continue
        artifacts = []
        if package.get("sdist"):
            artifacts.append(package["sdist"])
        artifacts.extend(package.get("wheels", []))
        if not artifacts:
            failures.append(f"{path}: registry package {name} has no locked artifacts")
        for artifact in artifacts:
            if SHA256.fullmatch(str(artifact.get("hash", ""))) is None:
                failures.append(f"{path}: package {name} has an artifact without SHA-256")


def check_actions(root: Path, failures: list[str]) -> None:
    workflows = sorted((root / ".github" / "workflows").glob("*.y*ml"))
    if not workflows:
        failures.append("no GitHub Actions workflows found")
    for workflow in workflows:
        text = workflow.read_text(encoding="utf-8")
        for action, revision in ACTION.findall(text):
            if action.startswith("./"):
                continue
            if COMMIT.fullmatch(revision) is None:
                failures.append(
                    f"{workflow.relative_to(root)}: {action} is not pinned to a full commit"
                )


def check_fetch_content(root: Path, failures: list[str]) -> None:
    cmake_files = [root / "CMakeLists.txt", *root.glob("cmake/**/*.cmake")]
    for path in cmake_files:
        if not path.is_file():
            continue
        for declaration in FETCH_CONTENT.findall(path.read_text(encoding="utf-8")):
            has_git_pin = re.search(r"\bGIT_TAG\s+[0-9a-f]{40}\b", declaration, re.IGNORECASE)
            has_url_hash = re.search(
                r"\bURL_HASH\s+SHA256=[0-9a-f]{64}\b",
                declaration,
                re.IGNORECASE,
            )
            if has_git_pin is None and has_url_hash is None:
                first_line = declaration.splitlines()[0].strip()
                failures.append(
                    f"{path.relative_to(root)}: {first_line} lacks a commit or SHA-256 pin"
                )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []
    check_west(root, failures)
    check_uv_lock(root / "uv.lock", failures)
    check_uv_lock(root / "tools" / "aster_cli" / "uv.lock", failures)
    check_actions(root, failures)
    check_fetch_content(root, failures)
    if failures:
        print("dependency pin check failed:", file=sys.stderr)
        print("\n".join(f"- {failure}" for failure in failures), file=sys.stderr)
        return 1
    print("dependency pin check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
