"""Deterministic workspace package editing and locking."""

from __future__ import annotations

import hashlib
import re
import subprocess
from pathlib import Path
from typing import Any

from .models import load_workspace
from .validation import (
    API_VERSION,
    canonical_json,
    dump_yaml,
    load_yaml,
    validate_mapping,
)


class PackageError(ValueError):
    pass


def _write(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    content = dump_yaml(document)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(path)


def add_package(
    workspace_path: str | Path,
    name: str,
    source: str,
    version: str | None = None,
    revision: str | None = None,
) -> None:
    path = Path(workspace_path).resolve()
    document = load_yaml(path)
    validate_mapping(document, "workspace.schema.json", str(path))
    packages = document["spec"]["packages"]
    if name in packages:
        raise PackageError(f"package {name!r} already exists")
    item: dict[str, Any] = {"source": source}
    if version:
        item["version"] = version
    if revision:
        item["revision"] = revision
    packages[name] = item
    validate_mapping(document, "workspace.schema.json", str(path))
    _write(path, document)


def remove_package(workspace_path: str | Path, name: str) -> None:
    path = Path(workspace_path).resolve()
    document = load_yaml(path)
    validate_mapping(document, "workspace.schema.json", str(path))
    packages = document["spec"]["packages"]
    if name not in packages:
        raise PackageError(f"package {name!r} does not exist")
    del packages[name]
    _write(path, document)


def list_packages(workspace_path: str | Path) -> list[dict[str, str | None]]:
    workspace = load_workspace(workspace_path)
    return [
        {
            "name": item.name,
            "source": item.source,
            "version": item.version,
            "revision": item.revision,
        }
        for item in workspace.packages
    ]


def _tree_digest(path: Path) -> str:
    digest = hashlib.sha256()
    ignored = {".git", ".venv", "__pycache__", "build", "dist"}
    if not path.exists():
        raise PackageError(f"package source does not exist: {path}")
    if path.is_file():
        digest.update(path.name.encode())
        digest.update(path.read_bytes())
        return digest.hexdigest()
    for item in sorted(
        candidate
        for candidate in path.rglob("*")
        if candidate.is_file() and not ignored.intersection(candidate.relative_to(path).parts)
    ):
        digest.update(item.relative_to(path).as_posix().encode())
        digest.update(b"\0")
        digest.update(item.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def _revision(path: Path, configured: str | None, digest: str) -> str:
    if configured:
        return configured
    root = path if path.is_dir() else path.parent
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
        return completed.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return digest


def _is_git_source(source: str) -> bool:
    lowered = source.lower()
    return lowered.startswith(("git+", "git@", "git://", "ssh://")) or (
        lowered.startswith(("http://", "https://")) and ".git" in lowered
    )


def _immutable_git_revision(revision: str | None) -> bool:
    return (
        revision is not None
        and re.fullmatch(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})", revision) is not None
    )


def _is_git_checkout(path: Path) -> bool:
    root = path if path.is_dir() else path.parent
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--is-inside-work-tree"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return False
    return completed.stdout.strip() == "true"


def lock_packages(
    workspace_path: str | Path,
    output: str | Path,
    *,
    release: bool = False,
) -> dict[str, Any]:
    workspace = load_workspace(workspace_path)
    packages: dict[str, Any] = {}
    for item in workspace.packages:
        if _is_git_source(item.source):
            if release and not _immutable_git_revision(item.revision):
                raise PackageError(
                    f"release lock requires immutable git revision for package {item.name!r}"
                )
            revision = item.revision or "unlocked"
            digest = hashlib.sha256(
                canonical_json({"source": item.source, "revision": revision})
            ).hexdigest()
        else:
            source = Path(item.source)
            resolved = (
                source.resolve()
                if source.is_absolute()
                else (workspace.source.parent / source).resolve()
            )
            digest = _tree_digest(resolved)
            revision = _revision(resolved, item.revision, digest)
            if release and _is_git_checkout(resolved) and not _immutable_git_revision(revision):
                raise PackageError(
                    f"release lock requires immutable git revision for package {item.name!r}"
                )
        packages[item.name] = {
            "source": item.source,
            "version": item.version or "0.0.0",
            "revision": revision,
            "digest": digest,
        }
    base = {
        "api_version": API_VERSION,
        "kind": "PackageLock",
        "metadata": {"name": workspace.metadata.name},
        "packages": packages,
    }
    document = dict(base)
    document["content_hash"] = hashlib.sha256(canonical_json(base)).hexdigest()
    validate_mapping(document, "package-lock.schema.json", str(output))
    _write(Path(output).resolve(), document)
    return document
