from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
from types import ModuleType

import pytest


def _load_generator() -> ModuleType:
    path = Path(__file__).parents[2] / "scripts" / "ci" / "generate_release_sbom.py"
    spec = importlib.util.spec_from_file_location("generate_release_sbom", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generator = _load_generator()
build_release_sbom = generator.build_release_sbom
main = generator.main


def test_release_sbom_inventories_every_asset_deterministically(tmp_path: Path) -> None:
    inputs = tmp_path / "inputs"
    inputs.mkdir()
    (inputs / "linux.tar.gz").write_bytes(b"linux")
    firmware = inputs / "firmware"
    firmware.mkdir()
    (firmware / "zephyr.tar.gz").write_bytes(b"zephyr")

    first = build_release_sbom(inputs, "v0.2.0-alpha.2")
    second = build_release_sbom(inputs, "v0.2.0-alpha.2")

    assert first == second
    assert first["metadata"]["component"]["version"] == "0.2.0-alpha.2"
    components = first["components"]
    assert [component["name"] for component in components] == [
        "firmware/zephyr.tar.gz",
        "linux.tar.gz",
    ]
    assert components[1]["hashes"] == [
        {"alg": "SHA-256", "content": hashlib.sha256(b"linux").hexdigest()}
    ]
    assert components[1]["properties"] == [
        {"name": "asterctrl:release-asset:size", "value": "5"}
    ]


def test_release_sbom_cli_writes_valid_json(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    inputs = tmp_path / "inputs"
    inputs.mkdir()
    (inputs / "metadata.tar.gz").write_bytes(b"metadata")
    output = tmp_path / "asterctrl.cdx.json"
    monkeypatch.setattr(
        "sys.argv",
        [
            "generate_release_sbom.py",
            str(inputs),
            str(output),
            "--version",
            "v0.2.0-alpha.2",
        ],
    )

    assert main() == 0
    document = json.loads(output.read_text(encoding="utf-8"))
    assert document["bomFormat"] == "CycloneDX"
    assert document["specVersion"] == "1.6"
    assert len(document["components"]) == 1


def test_release_sbom_rejects_an_empty_input_directory(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="is empty"):
        build_release_sbom(tmp_path, "v0.2.0-alpha.2")
