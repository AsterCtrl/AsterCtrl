from __future__ import annotations

import tomllib
from pathlib import Path

import pytest
import yaml
from aster_cli import __version__
from aster_cli.cli import main
from aster_cli.models import load_package_lock
from aster_cli.packages import (
    PackageError,
    add_package,
    list_packages,
    lock_packages,
    remove_package,
)
from aster_cli.validation import validate_document
from conftest import create_workspace


def test_only_aster_entrypoint_and_version() -> None:
    project = tomllib.loads(
        (Path(__file__).parents[2] / "tools/aster_cli/pyproject.toml").read_text(encoding="utf-8")
    )["project"]
    assert __version__ == "0.2.0-alpha.1"
    assert project["requires-python"] == ">=3.12,<3.13"
    assert project["scripts"] == {"aster": "aster_cli.cli:main"}
    assert main([]) == 0


def test_repository_package_manifest_matches_the_public_schema() -> None:
    root = Path(__file__).parents[2]
    document = validate_document(root / "package.yaml")

    assert document["metadata"]["version"] == "0.2.0-alpha.1"
    assert document["spec"]["build"]["target"] == "aster_core"


def test_package_lifecycle_and_lock_are_deterministic(tmp_path: Path) -> None:
    workspace, _, _, _ = create_workspace(tmp_path)
    extra = tmp_path / "extra"
    extra.mkdir()
    (extra / "data.txt").write_text("stable", encoding="utf-8")

    add_package(workspace, "extra", "extra", "1.0.0")
    assert [item["name"] for item in list_packages(workspace)] == ["control", "extra", "sensors"]
    first = lock_packages(workspace, tmp_path / "first.lock.yaml")
    second = lock_packages(workspace, tmp_path / "second.lock.yaml")
    assert first == second
    assert load_package_lock(tmp_path / "first.lock.yaml").content_hash == first["content_hash"]
    remove_package(workspace, "extra")
    assert [item["name"] for item in list_packages(workspace)] == ["control", "sensors"]


def test_build_defaults_to_plan(tmp_path: Path, capsys) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)

    assert main(["build", str(workspace), str(deployment)]) == 0
    assert '"mode": "plan"' in capsys.readouterr().out


def test_release_lock_requires_immutable_git_revision(tmp_path: Path) -> None:
    workspace, _, _, _ = create_workspace(tmp_path)
    document = yaml.safe_load(workspace.read_text(encoding="utf-8"))
    document["spec"]["packages"] = {"remote": {"source": "git+https://example.invalid/remote.git"}}
    workspace.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")

    development = lock_packages(workspace, tmp_path / "development.lock.yaml")
    assert development["packages"]["remote"]["revision"] == "unlocked"
    with pytest.raises(PackageError, match="immutable git revision"):
        lock_packages(workspace, tmp_path / "release.lock.yaml", release=True)

    revision = "0123456789abcdef0123456789abcdef01234567"
    document["spec"]["packages"]["remote"]["revision"] = revision
    workspace.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    release = lock_packages(workspace, tmp_path / "release.lock.yaml", release=True)
    assert release["packages"]["remote"]["revision"] == revision
