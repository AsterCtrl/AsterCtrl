from __future__ import annotations

from pathlib import Path

import pytest
import yaml
from aster_cli.models import load_deployment, load_hardware, load_module
from aster_cli.validation import ValidationError, validate_document
from conftest import create_workspace


def test_loads_typed_hardware_and_deployment(tmp_path: Path) -> None:
    _, _, hardware, deployment = create_workspace(tmp_path)

    assert load_hardware(hardware).board == "dev_c/stm32f407xx"
    assert load_hardware(hardware).platform == "zephyr"
    assert load_deployment(deployment).hosts[0].name == "mcu"


def test_rejects_unknown_fields_with_location(tmp_path: Path) -> None:
    workspace, _, _, _ = create_workspace(tmp_path)
    workspace.write_text(workspace.read_text() + "unknown: true\n", encoding="utf-8")

    with pytest.raises(ValidationError, match="unknown"):
        validate_document(workspace)


def test_rejects_old_api_version(tmp_path: Path) -> None:
    workspace, _, _, _ = create_workspace(tmp_path)
    workspace.write_text(workspace.read_text().replace("v1alpha2", "v1alpha1"), encoding="utf-8")

    with pytest.raises(ValidationError, match="v1alpha2"):
        validate_document(workspace)


def test_rejects_unqualified_zephyr_board_target(tmp_path: Path) -> None:
    _, _, hardware, _ = create_workspace(tmp_path)
    hardware.write_text(
        hardware.read_text().replace("dev_c/stm32f407xx", "dev_c"), encoding="utf-8"
    )

    with pytest.raises(ValidationError, match="board"):
        load_hardware(hardware)


def test_loads_watchdog_capability_and_resource(tmp_path: Path) -> None:
    _, _, hardware, _ = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = yaml.safe_load(module_path.read_text(encoding="utf-8"))
    module["spec"]["capabilities"] = [{"name": "safety-watchdog", "kind": "watchdog"}]
    module_path.write_text(yaml.safe_dump(module, sort_keys=False), encoding="utf-8")
    profile = yaml.safe_load(hardware.read_text(encoding="utf-8"))
    profile["spec"]["resources"]["watchdog"] = {
        "kind": "watchdog",
        "backend": "devicetree",
        "device": "iwdg",
        "options": {},
    }
    hardware.write_text(yaml.safe_dump(profile, sort_keys=False), encoding="utf-8")

    assert load_module(module_path).capabilities[0].kind == "watchdog"
    watchdog = next(item for item in load_hardware(hardware).resources if item.name == "watchdog")
    assert watchdog.kind == "watchdog"
    assert watchdog.device == "iwdg"


@pytest.mark.parametrize("document_kind", ["module", "hardware"])
def test_rejects_unknown_capability_resource_kind(tmp_path: Path, document_kind: str) -> None:
    _, _, hardware, _ = create_workspace(tmp_path)
    if document_kind == "module":
        source = tmp_path / "sensors/module.yaml"
        document = yaml.safe_load(source.read_text(encoding="utf-8"))
        document["spec"]["capabilities"] = [{"name": "timer", "kind": "timer"}]
        loader = load_module
    else:
        source = hardware
        document = yaml.safe_load(source.read_text(encoding="utf-8"))
        document["spec"]["resources"]["timer"] = {
            "kind": "timer",
            "backend": "devicetree",
            "device": "tim1",
        }
        loader = load_hardware
    source.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")

    with pytest.raises(ValidationError, match="timer"):
        loader(source)
