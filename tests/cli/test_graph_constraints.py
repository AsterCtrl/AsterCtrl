from __future__ import annotations

from pathlib import Path

import pytest
import yaml
from aster_cli.cli import main
from aster_cli.graph import GraphError, compile_application, resolve_deployment
from aster_cli.models import load_deployment_lock
from aster_cli.validation import ValidationError, dump_yaml
from conftest import create_workspace


def _load(path: Path) -> dict:
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def _save(path: Path, document: dict) -> None:
    path.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")


def test_rejects_startup_cycle(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    document = _load(application)
    document["spec"]["instances"]["imu"]["startup_after"] = ["controller"]
    document["spec"]["instances"]["controller"]["startup_after"] = ["imu"]
    _save(application, document)

    with pytest.raises(GraphError, match="startup dependency cycle"):
        compile_application(workspace, application)


def test_validates_instance_config_against_module_parameters(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = _load(module_path)
    module["spec"]["parameters"] = {
        "type": "object",
        "required": ["sample-rate-hz"],
        "properties": {"sample-rate-hz": {"type": "integer", "minimum": 1}},
        "additionalProperties": False,
    }
    _save(module_path, module)
    document = _load(application)
    document["spec"]["instances"]["imu"]["config"] = {"sample-rate-hz": 0}
    _save(application, document)

    with pytest.raises(GraphError, match=r"instance 'imu' config.sample-rate-hz: 0 is less"):
        compile_application(workspace, application)


def test_rejects_config_when_module_declares_no_parameters(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    document = _load(application)
    document["spec"]["instances"]["imu"]["config"] = {"sample-rate-hz": 100}
    _save(application, document)

    with pytest.raises(GraphError, match="supplies config.*declares no parameters"):
        compile_application(workspace, application)


def test_rejects_invalid_module_parameters_schema(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = _load(module_path)
    module["spec"]["parameters"] = {"type": "not-a-json-schema-type"}
    _save(module_path, module)

    with pytest.raises(GraphError, match="invalid parameters schema"):
        compile_application(workspace, application)


def test_rejects_external_module_parameter_schema_references(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = _load(module_path)
    module["spec"]["parameters"] = {"$ref": "https://example.com/parameters.schema.json"}
    _save(module_path, module)

    with pytest.raises(GraphError, match="only local fragment references are allowed"):
        compile_application(workspace, application)


def test_rejects_platform_incompatibility(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    module = tmp_path / "sensors/module.yaml"
    document = _load(module)
    document["spec"]["platforms"] = ["linux"]
    _save(module, document)

    with pytest.raises(GraphError, match="does not support zephyr"):
        resolve_deployment(workspace, deployment)


def test_rejects_duplicate_node_ids(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["nodes"]["sensor-node"]["id"] = 7
    document["spec"]["nodes"]["controller-node"]["id"] = 7
    _save(deployment, document)

    with pytest.raises(GraphError, match="node IDs.*duplicate"):
        resolve_deployment(workspace, deployment)


def test_rejects_duplicate_route_ids(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    sensor_path = tmp_path / "sensors/module.yaml"
    control_path = tmp_path / "control/module.yaml"
    sensor = _load(sensor_path)
    control = _load(control_path)
    app = _load(application)
    sensor["spec"]["ports"].append(
        {
            "name": "state2",
            "kind": "publisher",
            "type": "test.v1.State",
            "max_rate_hz": 100,
        }
    )
    control["spec"]["ports"].append(
        {
            "name": "state2",
            "kind": "subscriber",
            "type": "test.v1.State",
            "max_rate_hz": 100,
        }
    )
    app["spec"]["connections"][0]["id"] = 3
    app["spec"]["connections"].append(
        {
            "id": 3,
            "from": "imu.state2",
            "to": "controller.state2",
            "max_rate_hz": 100,
            "max_size": 32,
        }
    )
    _save(sensor_path, sensor)
    _save(control_path, control)
    _save(application, app)

    with pytest.raises(GraphError, match="route IDs.*duplicate"):
        compile_application(workspace, application)


def test_rejects_mixed_simulation_and_production_domains(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)
    app = _load(application)
    app["spec"]["domains"].append({"name": "simulation", "time": "simulated"})
    _save(application, app)
    deploy = _load(deployment)
    deploy["spec"]["nodes"]["sensor-node"]["domains"].append("simulation")
    deploy["spec"]["time"]["domains"]["simulation"] = {"source": "simulated"}
    _save(deployment, deploy)

    with pytest.raises(GraphError, match="mixes simulated and production"):
        resolve_deployment(workspace, deployment)


def test_rejects_stack_resource_overrun(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["budgets"]["hosts"]["mcu"]["stack_bytes"] = 512
    _save(deployment, document)

    with pytest.raises(GraphError, match="stack 1024 exceeds budget 512"):
        resolve_deployment(workspace, deployment)


@pytest.mark.parametrize(
    ("resource", "budget", "message"),
    (
        ("static_ram_bytes", {"ram_bytes": 1200}, "RAM 1536 exceeds budget 1200"),
        ("flash_bytes", {"flash_bytes": 2048}, "flash 4096 exceeds budget 2048"),
    ),
)
def test_rejects_declared_module_resource_overrun(
    tmp_path: Path, resource: str, budget: dict[str, int], message: str
) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = _load(module_path)
    module["spec"]["resources"] = {resource: 512 if resource == "static_ram_bytes" else 4096}
    _save(module_path, module)
    document = _load(deployment)
    document["spec"]["budgets"]["hosts"]["mcu"].update(budget)
    _save(deployment, document)

    with pytest.raises(GraphError, match=message):
        resolve_deployment(workspace, deployment)


def test_resolves_executor_backends_and_rejects_zephyr_worker_pool(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["nodes"]["controller-node"]["executors"] = {
        "control": {"policy": "worker_pool", "workers": 2}
    }
    _save(deployment, document)

    lock = resolve_deployment(workspace, deployment)

    assert lock["nodes"]["controller-node"]["executors"]["control"]["backend"] == (
        "linux_worker_pool"
    )
    assert lock["nodes"]["sensor-node"]["executors"]["control"]["backend"] == ("zephyr_work_queue")

    document["spec"]["nodes"]["sensor-node"]["executors"] = {
        "control": {"policy": "worker_pool", "workers": 2}
    }
    _save(deployment, document)
    with pytest.raises(GraphError, match="cannot use worker_pool on zephyr"):
        resolve_deployment(workspace, deployment)


def test_rejects_executor_capacity_below_task_requirements(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["nodes"]["sensor-node"]["executors"] = {
        "control": {"stack_bytes": 512, "queue_depth": 4}
    }
    _save(deployment, document)

    with pytest.raises(GraphError, match="stack 512 is below task requirement 1024"):
        resolve_deployment(workspace, deployment)


def test_rejects_link_budget_overrun(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["transports"]["can0"]["bitrate_bps"] = 1000
    _save(deployment, document)

    with pytest.raises(GraphError, match="utilization.*exceeds budget"):
        resolve_deployment(workspace, deployment)


def test_rejects_can_mtu_overrun(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["transports"]["can0"]["mtu"] = 9
    _save(deployment, document)

    with pytest.raises(GraphError, match="MTU 9 exceeds can limit 8"):
        resolve_deployment(workspace, deployment)


def test_official_transport_requires_hardware_on_every_endpoint(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["hosts"]["soc"].pop("hardware")
    _save(deployment, document)

    with pytest.raises(GraphError, match="requires a Hardware profile on soc"):
        resolve_deployment(workspace, deployment)


def test_rejects_incompatible_transport_backend(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    linux_hardware = tmp_path / "linux.hardware.yaml"
    document = _load(linux_hardware)
    document["spec"]["resources"]["bus"]["backend"] = "tty"
    document["spec"]["resources"]["bus"]["device"] = "/dev/ttyACM0"
    _save(linux_hardware, document)

    with pytest.raises(GraphError, match="TTY resource.*uart or usb_cdc"):
        resolve_deployment(workspace, deployment)


def test_resolves_unique_module_hardware_capability(tmp_path: Path) -> None:
    workspace, _, hardware, deployment = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = _load(module_path)
    module["spec"]["capabilities"] = [{"name": "control-bus", "kind": "can"}]
    _save(module_path, module)

    lock = resolve_deployment(workspace, deployment)

    assert lock["capability_bindings"]["imu"]["control-bus"] == {
        "kind": "can",
        "resource": "bus",
        "backend": "devicetree",
        "device": "can1",
        "options": {},
    }

    profile = _load(hardware)
    profile["spec"]["resources"]["secondary"] = {
        "kind": "can",
        "backend": "devicetree",
        "device": "can2",
        "options": {},
    }
    _save(hardware, profile)
    with pytest.raises(GraphError, match="multiple providers"):
        resolve_deployment(workspace, deployment)


def test_resolves_watchdog_capability_into_typed_lock(tmp_path: Path) -> None:
    workspace, _, hardware, deployment = create_workspace(tmp_path)
    module_path = tmp_path / "sensors/module.yaml"
    module = _load(module_path)
    module["spec"]["capabilities"] = [{"name": "safety-watchdog", "kind": "watchdog"}]
    _save(module_path, module)
    profile = _load(hardware)
    profile["spec"]["resources"]["watchdog"] = {
        "kind": "watchdog",
        "backend": "devicetree",
        "device": "iwdg",
        "options": {"timeout_ms": 1000},
    }
    _save(hardware, profile)

    lock = resolve_deployment(workspace, deployment)
    assert lock["capability_bindings"]["imu"]["safety-watchdog"] == {
        "kind": "watchdog",
        "resource": "watchdog",
        "backend": "devicetree",
        "device": "iwdg",
        "options": {"timeout_ms": 1000},
    }
    lock_path = tmp_path / "deployment.lock.yaml"
    lock_path.write_text(dump_yaml(lock), encoding="utf-8")
    typed_lock = load_deployment_lock(lock_path)
    binding = next(
        item for item in typed_lock.capability_bindings if item.capability == "safety-watchdog"
    )
    assert binding.kind == "watchdog"
    assert binding.device == "iwdg"


def test_rejects_missing_required_module_hardware_capability(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    module_path = tmp_path / "control/module.yaml"
    module = _load(module_path)
    module["spec"]["capabilities"] = [{"name": "debug-uart", "kind": "uart"}]
    _save(module_path, module)

    with pytest.raises(GraphError, match="requires missing hardware capability"):
        resolve_deployment(workspace, deployment)


@pytest.mark.parametrize(
    "options",
    ({}, {"vid": 0, "pid": 1}, {"vid": 1, "pid": 65536}),
)
def test_usb_cdc_requires_bounded_vid_and_pid(tmp_path: Path, options: dict[str, int]) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    transport = document["spec"]["transports"]["can0"]
    transport["type"] = "usb_cdc"
    transport["options"] = options
    _save(deployment, document)

    with pytest.raises(ValidationError, match="options"):
        resolve_deployment(workspace, deployment)


def test_rejects_undeclared_plugin_transport(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    document = _load(deployment)
    document["spec"]["transports"]["can0"]["type"] = "udp"
    _save(deployment, document)

    with pytest.raises(GraphError, match="requires a declarative backend and package"):
        resolve_deployment(workspace, deployment)


def test_accepts_package_exported_plugin_transport(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    package_path = tmp_path / "control/package.yaml"
    package = _load(package_path)
    package["spec"]["exports"]["transports"] = ["udp-socket"]
    _save(package_path, package)
    document = _load(deployment)
    transport = document["spec"]["transports"]["can0"]
    transport.update({"type": "udp", "backend": "udp-socket", "package": "control"})
    transport.pop("resource")
    _save(deployment, document)

    lock = resolve_deployment(workspace, deployment)

    assert lock["routes"][0]["transport"] == "can0"


def test_release_derives_schema_contract_without_declared_hash(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)

    graph = compile_application(workspace, application, release=True)
    lock = resolve_deployment(workspace, deployment, release=True)

    schema_hash = graph["connections"][0]["schema_hash"]
    assert len(schema_hash) == 64
    assert lock["routes"][0]["schema_hash"] == schema_hash
    assert lock["routes"][0]["schema_hash_source"] == "descriptor_bounds"
    assert lock["routes"][0]["max_encoded_size"] == 16


def test_release_derives_rpc_capacity_from_request_and_response(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)
    (tmp_path / "proto/state.proto").write_text(
        'syntax = "proto3";\n'
        "package test.v1;\n"
        "message Request { string command = 1; }\n"
        "message Response { bytes payload = 1; }\n"
        "service Control { rpc Exchange(Request) returns (Response); }\n",
        encoding="utf-8",
    )
    (tmp_path / "proto/bounds.yaml").write_text(
        "fields:\n"
        "  test.v1.Request.command: {max_size: 16}\n"
        "  test.v1.Response.payload: {max_size: 40}\n",
        encoding="utf-8",
    )
    sensor = _load(tmp_path / "sensors/module.yaml")
    sensor["spec"]["ports"][0].update({"kind": "rpc_client", "type": "test.v1.Control.Exchange"})
    _save(tmp_path / "sensors/module.yaml", sensor)
    control = _load(tmp_path / "control/module.yaml")
    control["spec"]["ports"][0].update({"kind": "rpc_server", "type": "test.v1.Control.Exchange"})
    _save(tmp_path / "control/module.yaml", control)
    app = _load(application)
    del app["spec"]["connections"][0]["max_size"]
    _save(application, app)

    graph = compile_application(workspace, application, release=True)
    lock = resolve_deployment(workspace, deployment, release=True)

    assert graph["connections"][0]["kind"] == "rpc"
    assert graph["connections"][0]["max_encoded_size"] == 42
    assert lock["routes"][0]["max_size"] == 42


def test_bounds_change_schema_hash_and_deployment_id(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    first = resolve_deployment(workspace, deployment, release=True)

    bounds = tmp_path / "proto/bounds.yaml"
    bounds.write_text("fields:\n  test.v1.State.label: {max_size: 9}\n", encoding="utf-8")
    second = resolve_deployment(workspace, deployment, release=True)

    assert first["routes"][0]["schema_hash"] != second["routes"][0]["schema_hash"]
    assert first["routes"][0]["max_encoded_size"] == 16
    assert second["routes"][0]["max_encoded_size"] == 17
    assert first["application_hash"] != second["application_hash"]
    assert first["deployment_id"] != second["deployment_id"]


def test_connection_capacity_defaults_to_derived_maximum(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)
    document = _load(application)
    del document["spec"]["connections"][0]["max_size"]
    _save(application, document)

    lock = resolve_deployment(workspace, deployment, release=True)

    assert lock["routes"][0]["max_size"] == 16
    assert lock["routes"][0]["max_encoded_size"] == 16


def test_rejects_connection_capacity_below_derived_maximum(tmp_path: Path) -> None:
    workspace, application, _, deployment = create_workspace(tmp_path)
    document = _load(application)
    document["spec"]["connections"][0]["max_size"] = 15
    _save(application, document)

    with pytest.raises(GraphError, match="max_size 15 is below encoded maximum 16"):
        resolve_deployment(workspace, deployment, release=True)


def test_release_rejects_packages_without_bounded_protobuf(tmp_path: Path) -> None:
    workspace, _, _, deployment = create_workspace(tmp_path)
    for path in (tmp_path / "sensors/package.yaml", tmp_path / "control/package.yaml"):
        package = _load(path)
        del package["spec"]["exports"]["protos"]
        del package["spec"]["protobuf"]
        _save(path, package)

    with pytest.raises(GraphError, match="requires exported bounded protobuf"):
        resolve_deployment(workspace, deployment, release=True)


def test_rejects_incorrect_declared_schema_hash_assertion(tmp_path: Path) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)
    for path in (tmp_path / "sensors/module.yaml", tmp_path / "control/module.yaml"):
        module = _load(path)
        module["spec"]["ports"][0]["schema_hash"] = "a" * 64
        _save(path, module)

    with pytest.raises(GraphError, match="schema_hash assertion does not match"):
        compile_application(workspace, application)


def test_graph_cli_outputs_clear_json_and_dot(tmp_path: Path, capsys) -> None:
    workspace, application, _, _ = create_workspace(tmp_path)

    assert main(["graph", str(workspace), str(application)]) == 0
    output = capsys.readouterr().out
    assert '"kind": "ApplicationGraph"' in output
    assert '"schema_hash"' in output
    assert '"startup_order"' in output

    assert (
        main(
            [
                "graph",
                str(workspace),
                str(application),
                "--format",
                "dot",
            ]
        )
        == 0
    )
    dot = capsys.readouterr().out
    assert "digraph application" in dot
    assert "#1 channel test.v1.State" in dot
