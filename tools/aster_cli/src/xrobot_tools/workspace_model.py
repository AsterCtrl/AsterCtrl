"""Workspace, Package, and Module resolution for deployment compilation."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from xrobot_tools.interface_model import InterfaceModel, load_interface_model
from xrobot_tools.validation import validate_document


class WorkspaceError(ValueError):
    """Raised when a workspace or package graph is inconsistent."""


def _resolve_workspace_path(root: Path, value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (root / path).resolve()


@dataclass(frozen=True)
class ModuleManifest:
    package_name: str
    package_path: Path
    name: str
    path: Path
    document: dict[str, Any]


@dataclass(frozen=True)
class BoardExport:
    package_name: str
    package_path: Path
    name: str
    document: dict[str, Any]


@dataclass(frozen=True)
class Package:
    name: str
    path: Path
    document: dict[str, Any]
    modules: dict[str, ModuleManifest]
    boards: dict[str, BoardExport]


class Workspace:
    def __init__(self, workspace_path: str | Path) -> None:
        self.path = Path(workspace_path).resolve()
        self.root = self.path.parent
        self.document = validate_document(self.path)
        lock_path = self.root / "package.lock.yaml"
        if not lock_path.is_file():
            raise WorkspaceError(f"{lock_path}: package lock is required")
        self.lock = validate_document(lock_path)
        if self.lock["metadata"]["workspace"] != self.document["metadata"]["name"]:
            raise WorkspaceError("package lock belongs to a different workspace")

        self._sources: dict[str, Path] = {}
        self._manifest_overrides: dict[str, Path] = {}
        for item in self.document["packages"]:
            name = item["name"]
            if name in self._sources:
                raise WorkspaceError(f"duplicate workspace package {name!r}")
            source_path = Path(item["source"]["path"])
            if not source_path.is_absolute():
                source_path = self.root / source_path
            self._sources[name] = source_path.resolve()
            manifest = item["source"].get("manifest")
            if manifest is not None:
                self._manifest_overrides[name] = _resolve_workspace_path(
                    self.root, manifest
                )

        locked = self.lock["packages"]
        missing_locks = sorted(set(self._sources) - set(locked))
        if missing_locks:
            raise WorkspaceError(
                f"packages missing from package.lock.yaml: {', '.join(missing_locks)}"
            )
        self._packages: dict[str, Package] = {}
        self._loading: set[str] = set()

    def package(self, name: str) -> Package:
        if name in self._packages:
            return self._packages[name]
        if name not in self._sources:
            raise WorkspaceError(f"package {name!r} is not declared by workspace.yaml")
        if name in self._loading:
            raise WorkspaceError(f"cyclic package dependency involving {name!r}")
        self._loading.add(name)
        path = self._sources[name]
        manifest_path = self._manifest_overrides.get(name, path / "package.yaml")
        if not manifest_path.is_file():
            raise WorkspaceError(f"{manifest_path}: package manifest not found")
        document = validate_document(manifest_path)
        if document["metadata"]["name"] != name:
            raise WorkspaceError(
                f"{manifest_path}: package name does not match workspace entry {name!r}"
            )
        self._verify_git_lock(name, path)

        for dependency in document["spec"].get("dependencies", []):
            dependency_name = dependency if isinstance(dependency, str) else dependency["name"]
            self.package(dependency_name)

        modules: dict[str, ModuleManifest] = {}
        for export in document["spec"].get("exports", {}).get("modules", []):
            module_name = export["name"]
            module_path = (path / export["manifest"]).resolve()
            module_document = validate_document(module_path)
            if module_document["metadata"]["name"] != module_name:
                raise WorkspaceError(
                    f"{module_path}: module name does not match export {module_name!r}"
                )
            if module_name in modules:
                raise WorkspaceError(f"{manifest_path}: duplicate module {module_name!r}")
            modules[module_name] = ModuleManifest(
                package_name=name,
                package_path=path,
                name=module_name,
                path=module_path,
                document=module_document,
            )
        boards: dict[str, BoardExport] = {}
        for export in document["spec"].get("exports", {}).get("boards", []):
            board_name = export["name"]
            if board_name in boards:
                raise WorkspaceError(
                    f"{manifest_path}: duplicate board export {board_name!r}"
                )
            boards[board_name] = BoardExport(
                package_name=name,
                package_path=path,
                name=board_name,
                document=export,
            )
        package = Package(
            name=name,
            path=path,
            document=document,
            modules=modules,
            boards=boards,
        )
        self._packages[name] = package
        self._loading.remove(name)
        return package

    def module(self, package_name: str, module_name: str) -> ModuleManifest:
        package = self.package(package_name)
        if module_name not in package.modules:
            raise WorkspaceError(
                f"package {package_name!r} does not export module {module_name!r}"
            )
        return package.modules[module_name]

    def has_package(self, name: str) -> bool:
        return name in self._sources

    @property
    def package_paths(self) -> dict[str, Path]:
        return dict(self._sources)

    def board(self, reference: str) -> BoardExport:
        package_name, separator, board_name = reference.partition("/")
        if not separator or not package_name or not board_name:
            raise WorkspaceError(
                f"board reference {reference!r} must be package/export-name"
            )
        package = self.package(package_name)
        if board_name not in package.boards:
            raise WorkspaceError(
                f"package {package_name!r} does not export board {board_name!r}"
            )
        return package.boards[board_name]

    def interface_model(self) -> InterfaceModel:
        package = self.package("robot-msgs")
        return load_interface_model(package.path / "schemas")

    def _verify_git_lock(self, name: str, path: Path) -> None:
        if not (path / ".git").exists():
            return
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
        actual = result.stdout.strip()
        expected = self.lock["packages"][name]["commit"]
        if actual != expected:
            raise WorkspaceError(
                f"package {name!r} is at {actual}, but package.lock.yaml requires {expected}"
            )
