from __future__ import annotations

import subprocess
from pathlib import Path

import pytest
import yaml

from xrobot_tools.deployment import DeploymentError, compile_deployment


def write(root: Path, relative: str, content: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def create_workspace(tmp_path: Path, max_rate_hz: int = 100) -> tuple[Path, Path]:
    write(
        tmp_path,
        "robot-msgs/package.yaml",
        """\
api_version: xrobot.io/v1alpha1
kind: Package
metadata: {name: robot-msgs, version: 0.1.0, license: Apache-2.0}
spec:
  build: {system: xrobot-schema}
  exports: {schemas: [schemas/msg]}
  dependencies: []
""",
    )
    write(
        tmp_path,
        "robot-msgs/schemas/msg/Command.msg.yaml",
        """\
api_version: xrobot.io/schema/v1alpha1
kind: Message
metadata: {name: Command, namespace: test.msg}
spec:
  fields:
    - {name: value, type: uint16}
""",
    )
    for package, kind in (("source", "publisher"), ("sink", "subscriber")):
        write(
            tmp_path,
            f"{package}/package.yaml",
            f"""\
api_version: xrobot.io/v1alpha1
kind: Package
metadata: {{name: {package}, version: 0.1.0, license: Apache-2.0}}
spec:
  build: {{system: cmake}}
  exports:
    modules:
      - {{name: {package}, manifest: module.yaml}}
  dependencies: [robot-msgs]
""",
        )
        write(
            tmp_path,
            f"{package}/module.yaml",
            f"""\
api_version: xrobot.io/v1alpha1
kind: Module
metadata: {{name: {package}, version: 0.1.0}}
spec:
  implementation: {{target: {package}, class: test::{package.title()}}}
  dependencies: []
  ports:
    - {{name: command, kind: {kind}, type: test.msg.Command, required: true}}
  executors:
    - {{name: control, priority: 4, stack_bytes: 1024, queue_depth: 4, period_us: 1000}}
  parameters: []
  hardware: []
""",
        )

    write(
        tmp_path,
        "workspace.yaml",
        """\
api_version: xrobot.io/v1alpha1
kind: Workspace
metadata: {name: test-workspace}
packages:
  - {name: robot-msgs, source: {type: path, path: robot-msgs}}
  - {name: source, source: {type: path, path: source}}
  - {name: sink, source: {type: path, path: sink}}
""",
    )
    write(
        tmp_path,
        "package.lock.yaml",
        """\
api_version: xrobot.io/v1alpha1
kind: PackageLock
metadata: {workspace: test-workspace}
packages:
  robot-msgs: {source: robot-msgs, commit: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa}
  source: {source: source, commit: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb}
  sink: {source: sink, commit: cccccccccccccccccccccccccccccccccccccccc}
""",
    )
    write(
        tmp_path,
        "robot.yaml",
        """\
api_version: xrobot.io/v1alpha1
kind: Robot
metadata: {name: test-robot}
instances:
  command_source:
    package: source
    module: source
    ports: {command: /control/command}
  command_sink:
    package: sink
    module: sink
    ports: {command: /control/command}
""",
    )
    for node in ("node_a", "node_b"):
        write(
            tmp_path,
            f"hardware/{node}.yaml",
            f"""\
api_version: xrobot.io/v1alpha1
kind: HardwareProfile
metadata: {{name: {node}, board: test/{node}}}
spec:
  resources:
    robot_bus: {{kind: can, backend: libxr, resource: can1, options: {{}}}}
  devices: {{}}
""",
        )
    write(
        tmp_path,
        "deployment.yaml",
        f"""\
api_version: xrobot.io/v1alpha1
kind: Deployment
metadata: {{name: dual-node}}
application: robot.yaml
time_authority: node_a
nodes:
  node_a:
    runtime: xrobot-mcu
    target: {{bsp: test/a, hardware: hardware/node_a.yaml, profile: debug}}
    instances: [command_source]
  node_b:
    runtime: xrobot-mcu
    target: {{bsp: test/b, hardware: hardware/node_b.yaml, profile: debug}}
    instances: [command_sink]
links:
  robot_can:
    transport: xrobot-can
    endpoints:
      - {{node: node_a, resource: robot_bus}}
      - {{node: node_b, resource: robot_bus}}
    options: {{frame: classic, bitrate_bps: 1000000, mtu_bytes: 8}}
    budget: {{utilization_limit: 0.65}}
qos_profiles:
  control:
    class: control
    delivery: latest
    reliability: best_effort
    history_depth: 1
    max_rate_hz: {max_rate_hz}
    deadline_ms: 10
    lifespan_ms: 30
    max_age_ms: 20
    on_stale: zero
    rearm: fresh_sample
    adaptive: false
route_rules:
  - match: {{topic: /control/**}}
    qos: control
reserved_bandwidth: {{robot_can: 0.20}}
""",
    )
    return tmp_path / "workspace.yaml", tmp_path / "deployment.yaml"


def test_compiles_a_deterministic_cross_node_deployment(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path)
    output = tmp_path / "generated"

    first = compile_deployment(workspace, deployment, output)
    first_lock = (output / "deployment.lock.yaml").read_bytes()
    first_routes = (output / "reports/routes.json").read_bytes()
    second = compile_deployment(workspace, deployment, output)

    assert first == second
    assert (output / "deployment.lock.yaml").read_bytes() == first_lock
    assert (output / "reports/routes.json").read_bytes() == first_routes
    lock = yaml.safe_load(first_lock)
    assert lock["nodes"] == {"node_a": 1, "node_b": 2}
    assert lock["routes"] == {"/control/command": 1}
    assert first.cross_node_route_count == 1
    assert first.node_count == 2
    assert (output / "nodes/node_a/generated_main.cpp").is_file()
    assert (output / "nodes/node_b/generated_main.cpp").is_file()
    for node in ("node_a", "node_b"):
        node_dir = output / "nodes" / node
        subprocess.run(
            [
                "/usr/bin/c++",
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Wconversion",
                "-Werror",
                "-I",
                str(node_dir),
                "-c",
                str(node_dir / "generated_main.cpp"),
                "-o",
                str(node_dir / "generated_main.o"),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    budget = yaml.safe_load((output / "reports/link_budget.yaml").read_text())
    assert budget["links"]["robot_can"]["within_budget"] is True


def test_rejects_an_instance_placed_more_than_once(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path)
    text = deployment.read_text(encoding="utf-8")
    deployment.write_text(
        text.replace("instances: [command_sink]", "instances: [command_source, command_sink]"),
        encoding="utf-8",
    )

    with pytest.raises(DeploymentError, match="command_source.*more than once"):
        compile_deployment(workspace, deployment, tmp_path / "generated")


def test_rejects_a_classic_can_budget_overflow(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path, max_rate_hz=10000)

    with pytest.raises(DeploymentError, match="robot_can.*utilization"):
        compile_deployment(workspace, deployment, tmp_path / "generated")
