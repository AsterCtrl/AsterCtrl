from __future__ import annotations

from pathlib import Path

import pytest

from aster_tools.validation import ValidationError, validate_document


WORKSPACE = """\
api_version: aster.dev/v1alpha1
kind: Workspace
metadata:
  name: test-workspace
interfaces:
  package: robot-msgs
  path: schemas
packages:
  - name: aster-runtime
    source:
      type: path
      path: ../aster-runtime
"""


def test_validates_a_workspace(tmp_path: Path) -> None:
    path = tmp_path / "workspace.yaml"
    path.write_text(WORKSPACE, encoding="utf-8")

    document = validate_document(path)

    assert document["kind"] == "Workspace"
    assert document["metadata"]["name"] == "test-workspace"


def test_rejects_unknown_keys_with_a_precise_path(tmp_path: Path) -> None:
    path = tmp_path / "workspace.yaml"
    path.write_text(WORKSPACE + "unexpected: true\n", encoding="utf-8")

    with pytest.raises(ValidationError, match=r"workspace.yaml: unexpected"):
        validate_document(path)


def test_rejects_unknown_document_kinds(tmp_path: Path) -> None:
    path = tmp_path / "unknown.yaml"
    path.write_text(
        "api_version: aster.dev/v1alpha1\nkind: Mystery\nmetadata: {name: x}\n",
        encoding="utf-8",
    )

    with pytest.raises(ValidationError, match="unsupported kind 'Mystery'"):
        validate_document(path)
