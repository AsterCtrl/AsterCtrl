from __future__ import annotations

import tomllib
from pathlib import Path

import pytest
from aster_cli import __version__
from aster_cli.cli import main
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


@pytest.mark.parametrize("command", ["add", "remove", "list", "lock"])
def test_package_management_reports_migration_to_native_tools(command, capsys) -> None:
    assert main(["package", command]) == 2
    assert "CMake, west and uv" in capsys.readouterr().err


def test_build_defaults_to_plan(tmp_path: Path, capsys) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)

    assert main(["build", str(workspace), str(deployment)]) == 0
    output = capsys.readouterr()
    assert '"mode": "plan"' in output.out
    assert "legacy v1alpha2" in output.err
