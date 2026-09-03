from pathlib import Path

import pytest

from aster_tools.hardware_plugin import (
    HardwarePluginError,
    load_hardware_providers,
)


def _write_provider_package(root: Path, name: str, driver: str) -> Path:
    package = root / name
    (package / "tools").mkdir(parents=True)
    (package / "package.yaml").write_text(
        f"""\
api_version: aster.dev/v1alpha1
kind: Package
metadata: {{name: {name}, version: 0.1.0, license: Apache-2.0}}
spec:
  build: {{system: cmake}}
  exports:
    hardware_drivers:
      - {{driver: {driver}, provider: tools/provider.py, factory: provider}}
  dependencies: []
""",
        encoding="utf-8",
    )
    (package / "tools/provider.py").write_text(
        f"""\
from aster_tools.hardware_plugin import DeviceFragment, HardwareDriverProvider

def provider():
    return HardwareDriverProvider(
        driver={driver!r},
        resources={{"bus": "can"}},
        provided_type="example.hardware.Device/v1",
        build_package={name!r},
        build_target="example::driver",
        render=lambda context: DeviceFragment((), (), ()),
    )
""",
        encoding="utf-8",
    )
    return package


def test_loads_provider_from_package_export(tmp_path: Path) -> None:
    package = _write_provider_package(tmp_path, "example-driver", "example/can")

    providers = load_hardware_providers({"example-driver": package})

    assert providers["example/can"].provided_type == "example.hardware.Device/v1"
    assert providers["example/can"].resources == {"bus": "can"}


def test_rejects_duplicate_driver_exports(tmp_path: Path) -> None:
    first = _write_provider_package(tmp_path, "driver-a", "example/can")
    second = _write_provider_package(tmp_path, "driver-b", "example/can")

    with pytest.raises(HardwarePluginError, match="exported more than once"):
        load_hardware_providers({"driver-a": first, "driver-b": second})


def test_rejects_provider_outside_package(tmp_path: Path) -> None:
    package = _write_provider_package(tmp_path, "example-driver", "example/can")
    manifest = package / "package.yaml"
    manifest.write_text(
        manifest.read_text(encoding="utf-8").replace(
            "tools/provider.py", "../outside.py"
        ),
        encoding="utf-8",
    )

    with pytest.raises(HardwarePluginError, match="escapes the Package"):
        load_hardware_providers({"example-driver": package})


def test_generic_codegen_does_not_embed_product_drivers() -> None:
    source = (
        Path(__file__).parents[1] / "src/aster_tools/hardware_codegen.py"
    ).read_text(encoding="utf-8")
    for product_name in (
        "bmi088/spi",
        "motor/dji-group",
        "motor/dm-group",
        "supercap-ctrl/shu-can",
        "referee/ui-writer",
        "vision-link/srm-vcp",
    ):
        assert product_name not in source
