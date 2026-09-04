from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml
from aster_cli.emitters import emit_deployment
from aster_cli.graph import GraphError, compile_application, resolve_deployment
from aster_cli.models import load_deployment_lock
from conftest import create_workspace


def test_compiles_and_resolves_deterministically(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)
    graph = compile_application(workspace, application)
    first = resolve_deployment(workspace, deployment)
    second = resolve_deployment(workspace, deployment)

    assert graph["connections"][0]["type"] == "test.v1.State"
    assert first == second
    assert first["routes"][0]["transport"] == "can0"
    assert first["routes"][0]["id"] == 1
    assert first["routes"][0]["schema_hash_source"] == "descriptor_bounds"
    assert first["routes"][0]["schema_hash"] == first["routes"][0]["schema_input_digest"]
    assert first["routes"][0]["max_encoded_size"] == 16
    assert first["routes"][0]["max_size"] == 32


def test_rejects_missing_transport(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    text = deployment.read_text(encoding="utf-8")
    text = text.replace(
        "  transports:\n"
        "    can0: {type: can, hosts: [mcu, soc], bitrate_bps: 1000000, "
        "mtu: 8, resource: bus}\n",
        "",
    )
    deployment.write_text(text.replace("    transports: {can0: 0.8}\n", ""), encoding="utf-8")

    with pytest.raises(GraphError, match="no transport connects"):
        resolve_deployment(workspace, deployment)


def test_rejects_incompatible_provider_requirement(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    sensor_path = tmp_path / "sensors/module.yaml"
    control_path = tmp_path / "control/module.yaml"
    sensor = yaml.safe_load(sensor_path.read_text(encoding="utf-8"))
    control = yaml.safe_load(control_path.read_text(encoding="utf-8"))
    app = yaml.safe_load(application.read_text(encoding="utf-8"))
    sensor["spec"]["provides"] = [
        {"name": "clock", "interface": "test.Clock/v1", "domain": "control"}
    ]
    control["spec"]["requires"] = [
        {"name": "clock", "interface": "test.OtherClock/v1", "domain": "control"}
    ]
    app["spec"]["bindings"] = [{"requirement": "controller.clock", "provider": "imu.clock"}]
    sensor_path.write_text(yaml.safe_dump(sensor, sort_keys=False), encoding="utf-8")
    control_path.write_text(yaml.safe_dump(control, sort_keys=False), encoding="utf-8")
    application.write_text(yaml.safe_dump(app, sort_keys=False), encoding="utf-8")

    with pytest.raises(GraphError, match="interface mismatch"):
        compile_application(workspace, application)


def test_linux_and_zephyr_emitters_are_bounded(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)
    sensor_module = tmp_path / "sensors/module.yaml"
    sensor_document = yaml.safe_load(sensor_module.read_text(encoding="utf-8"))
    sensor_document["spec"]["capabilities"] = [{"name": "control-bus", "kind": "can"}]
    sensor_document["spec"]["parameters"] = {
        "type": "object",
        "properties": {"gain": {"type": "integer"}},
        "additionalProperties": False,
    }
    sensor_document["spec"]["resources"] = {
        "static_ram_bytes": 256,
        "flash_bytes": 4096,
    }
    sensor_module.write_text(yaml.safe_dump(sensor_document, sort_keys=False), encoding="utf-8")
    control_module = tmp_path / "control/module.yaml"
    control_document = yaml.safe_load(control_module.read_text(encoding="utf-8"))
    control_document["spec"]["parameters"] = sensor_document["spec"]["parameters"]
    control_document["spec"]["resources"] = {
        "static_ram_bytes": 128,
        "flash_bytes": 2048,
    }
    control_module.write_text(yaml.safe_dump(control_document, sort_keys=False), encoding="utf-8")
    application_document = yaml.safe_load(application.read_text(encoding="utf-8"))
    application_document["spec"]["instances"]["imu"]["config"] = {"gain": 2}
    application_document["spec"]["instances"]["controller"]["config"] = {"gain": 3}
    application.write_text(yaml.safe_dump(application_document, sort_keys=False), encoding="utf-8")
    output = tmp_path / "generated"
    lock = emit_deployment(workspace, deployment, output)

    assert (output / "deployment.lock.yaml").is_file()
    bounded_header = output / "types/state.pb.hpp"
    assert bounded_header.is_file()
    assert 'kSchemaSha256 = "' in bounded_header.read_text(encoding="utf-8")
    zephyr_config = (output / "nodes/sensor-node/aster.generated.conf").read_text()
    assert "CONFIG_CPP=y" in zephyr_config
    assert "CONFIG_STD_CPP20=y" in zephyr_config
    assert "CONFIG_REQUIRES_FULL_LIBCPP=y" in zephyr_config
    assert "CONFIG_ASTERCTRL_ROUTE_COUNT=1" in zephyr_config
    assert "CONFIG_CAN=y" in zephyr_config
    assert (
        "aster-bus = &can1;" in (output / "nodes/sensor-node/aster.generated.overlay").read_text()
    )
    linux_config = yaml.safe_load(
        (output / "nodes/controller-node/aster.generated.yaml").read_text()
    )
    assert linux_config["instances"][0]["config"] == {"gain": 3}
    assert linux_config["instances"][0]["config_json"] == '{"gain":3}'
    assert linux_config["hardware"]["resources"]["bus"] == {
        "backend": "socketcan",
        "device": "can0",
        "kind": "can",
        "options": {},
    }
    assert (
        "target_sources(app PRIVATE"
        in (output / "nodes/sensor-node/aster.generated.cmake").read_text()
    )
    assert (
        "add_library(aster_generated"
        in (output / "nodes/controller-node/aster.generated.cmake").read_text()
    )
    assert lock["stack_bytes"] == {"mcu": 1024, "soc": 1024}
    assert lock["static_ram_bytes"] == {"mcu": 256, "soc": 128}
    assert lock["flash_bytes"] == {"mcu": 4096, "soc": 2048}
    assert lock["resource_budgets"]["hosts"]["mcu"]["ram_bytes"]["used"] == 1280
    assert lock["resource_budgets"]["hosts"]["soc"]["flash_bytes"]["used"] == 2048
    typed_lock = load_deployment_lock(output / "deployment.lock.yaml")
    assert typed_lock.routes[0].id == 1
    assert len(typed_lock.routes[0].schema_hash) == 64
    assert typed_lock.routes[0].schema_hash_source == "descriptor_bounds"
    assert typed_lock.routes[0].schema_input_digest == typed_lock.routes[0].schema_hash
    assert typed_lock.routes[0].max_encoded_size == 16
    assert typed_lock.nodes[0].node_id == 1
    assert typed_lock.nodes[0].name == "controller-node"
    assert typed_lock.nodes[0].executors[0].backend == "linux_thread"
    assert typed_lock.hosts[0].os == "zephyr"
    assert typed_lock.hardware[0].resources[0].backend == "devicetree"
    assert typed_lock.hardware[1].resources[0].device == "can0"
    assert typed_lock.transports[0].resource == "bus"
    assert typed_lock.capability_bindings[0].capability == "control-bus"
    assert typed_lock.capability_bindings[0].device == "can1"
    assert typed_lock.artifacts[0].digest_kind == "inputs"
    assert typed_lock.artifacts[0].artifact_digest is None
    assert typed_lock.host_budgets[0].stack_bytes.used == 1024
    assert typed_lock.static_ram_bytes == {"mcu": 256, "soc": 128}
    assert typed_lock.flash_bytes == {"mcu": 4096, "soc": 2048}
    compiler = shutil.which("c++")
    if compiler:
        include = Path(__file__).parents[2] / "include"
        for source in output.glob("nodes/*/composition.generated.cpp"):
            header = source.with_suffix(".hpp")
            assert "ModuleSlot" in header.read_text(encoding="utf-8")
            assert "kDeploymentId" in header.read_text(encoding="utf-8")
            assert "schema_input_digest" in header.read_text(encoding="utf-8")
            assert "max_encoded_size" in header.read_text(encoding="utf-8")
            assert "kExecutors" in header.read_text(encoding="utf-8")
            assert "config_json" in header.read_text(encoding="utf-8")
            check = source.with_name("composition.check.cpp")
            check.write_text(
                '#include "aster/configuration.hpp"\n'
                '#include "composition.generated.hpp"\n'
                "int main() {\n"
                "  aster::StaticConfigurator<1, sizeof(std::uint32_t)> fallback;\n"
                "  const std::uint32_t marker = 42;\n"
                '  if (fallback.Put("global", marker) != aster::Status::kOk ||\n'
                "      fallback.Seal() != aster::Status::kOk) return 1;\n"
                "  auto handles = aster::CoreHandles{};\n"
                "  handles.configurator = aster::ConfiguratorRef(fallback);\n"
                "  const aster::CoreRef core(handles);\n"
                "  aster::generated::Composition value(core);\n"
                "  if (value.Modules().size() != 1) return 1;\n"
                "  std::uint32_t loaded{};\n"
                '  if (value.Modules()[0].core.configurator().Get("global", loaded) !=\n'
                "          aster::Status::kOk ||\n"
                "      loaded != marker) return 2;\n"
                "  std::array<std::byte, 64> output{};\n"
                "  std::size_t written{};\n"
                "  const auto status = value.Modules()[0].core.configurator().Get(\n"
                "      aster::generated::kInstanceConfigKey, output, written);\n"
                "  if (status != aster::Status::kOk) return 3;\n"
                "  const auto config = std::string_view(\n"
                "      reinterpret_cast<const char*>(output.data()), written);\n"
                "  return config == aster::generated::kInstances[0].config_json ? 0 : 4;\n"
                "}\n",
                encoding="utf-8",
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(include),
                    "-I",
                    str(tmp_path),
                    str(check),
                    "-o",
                    str(source.with_suffix(".check")),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(source.with_suffix(".check"))], check=True)


@pytest.mark.parametrize(
    ("shared_declaration", "field_declaration", "expected_type", "bounds_document"),
    [
        (
            "message Common { string label = 1; }",
            "shared.v1.Common common = 1;",
            "shared.v1.Common",
            "fields:\n  shared.v1.Common.label: {max_size: 8}\n",
        ),
        (
            "enum Mode { MODE_UNSPECIFIED = 0; MODE_READY = 1; }",
            "shared.v1.Mode mode = 1;",
            "shared.v1.Mode",
            "fields: {}\n",
        ),
    ],
)
def test_deployment_codegen_rejects_overlapping_generated_contracts(
    tmp_path: Path,
    shared_declaration: str,
    field_declaration: str,
    expected_type: str,
    bounds_document: str,
) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    proto = tmp_path / "overlap"
    proto.mkdir()
    (proto / "common.proto").write_text(
        f'syntax = "proto3"; package shared.v1; {shared_declaration}\n',
        encoding="utf-8",
    )
    for package in ("alpha", "beta"):
        (proto / f"{package}.proto").write_text(
            'syntax = "proto3"; '
            f"package {package}.v1; "
            'import "common.proto"; '
            f"message {package.title()} {{ {field_declaration} }}\n",
            encoding="utf-8",
        )
        package_root = tmp_path / package
        package_root.mkdir()
        (package_root / "package.yaml").write_text(
            "api_version: aster.dev/v1alpha2\n"
            "kind: Package\n"
            f"metadata: {{name: {package}, version: 0.2.0}}\n"
            "spec:\n"
            "  build: {system: header-only}\n"
            f"  exports: {{protos: [../overlap/{package}.proto]}}\n"
            "  protobuf:\n"
            "    bounds: ../overlap/bounds.yaml\n"
            "    includes: [../overlap]\n",
            encoding="utf-8",
        )
    (proto / "bounds.yaml").write_text(bounds_document, encoding="utf-8")
    workspace_document = yaml.safe_load(workspace.read_text(encoding="utf-8"))
    workspace_document["spec"]["packages"].update(
        {"alpha": {"source": "alpha"}, "beta": {"source": "beta"}}
    )
    workspace.write_text(yaml.safe_dump(workspace_document, sort_keys=False), encoding="utf-8")

    with pytest.raises(ValueError, match=rf"{expected_type}.*consolidate"):
        emit_deployment(workspace, deployment, tmp_path / "generated")


def test_zephyr_usb_emitter_selects_new_stack_and_product_ids(tmp_path: Path) -> None:
    workspace, _, zephyr_hardware, deployment = create_workspace(tmp_path)
    zephyr = yaml.safe_load(zephyr_hardware.read_text(encoding="utf-8"))
    zephyr["spec"]["resources"]["bus"].update({"kind": "usb_cdc", "device": "cdc_acm_uart0"})
    zephyr_hardware.write_text(yaml.safe_dump(zephyr, sort_keys=False), encoding="utf-8")
    linux_hardware = tmp_path / "linux.hardware.yaml"
    linux = yaml.safe_load(linux_hardware.read_text(encoding="utf-8"))
    linux["spec"]["resources"]["bus"].update(
        {"kind": "usb_cdc", "backend": "tty", "device": "/dev/ttyACM0"}
    )
    linux_hardware.write_text(yaml.safe_dump(linux, sort_keys=False), encoding="utf-8")
    deployment_document = yaml.safe_load(deployment.read_text(encoding="utf-8"))
    deployment_document["spec"]["transports"]["can0"].update(
        {"type": "usb_cdc", "mtu": 64, "options": {"vid": 0xCAFE, "pid": 0x4001}}
    )
    deployment.write_text(yaml.safe_dump(deployment_document, sort_keys=False), encoding="utf-8")

    output = tmp_path / "generated-usb"
    emit_deployment(workspace, deployment, output)

    config = (output / "nodes/sensor-node/aster.generated.conf").read_text()
    overlay = (output / "nodes/sensor-node/aster.generated.overlay").read_text()
    assert "CONFIG_USB_DEVICE_STACK_NEXT=y" in config
    assert "CONFIG_USBD_CDC_ACM_CLASS=y" in config
    assert "CONFIG_ASTERCTRL_USB_VID=0xCAFE" in config
    assert "CONFIG_ASTERCTRL_USB_PID=0x4001" in config
    assert "aster-bus = &cdc_acm_uart0;" in overlay
    assert '&cdc_acm_uart0 {\n  status = "okay";\n};' in overlay
