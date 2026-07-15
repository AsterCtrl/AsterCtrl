from __future__ import annotations

import subprocess
from pathlib import Path

import pytest
import yaml

from xrobot_tools.deployment import (
    DeploymentError,
    _can_route_cost,
    compile_deployment,
)
from xrobot_tools.interfaces import generate_interfaces


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
    - {name: auxiliary, type: uint32}
""",
    )
    write(
        tmp_path,
        "source/include/test/source.hpp",
        """\
#pragma once

#include <cstdint>
#include <string_view>

#include "robot_msgs/robot_msgs.hpp"
#include "xrobot/runtime/module.hpp"

namespace test {

class Device {
 public:
  static constexpr std::string_view TypeName() noexcept {
    return "test.hardware.SourceDevice/v1";
  }

  std::uint16_t bias{};
};

class Source final : public xrobot::runtime::Module {
 public:
  explicit Source(std::string_view name) noexcept : name_(name) {}

  std::string_view Name() const noexcept override { return name_; }

  xrobot::runtime::Status Initialize(
      xrobot::runtime::ModuleContext& context) noexcept override {
    using xrobot::runtime::IsOk;
    if (auto status = context.ResolveTopicPublisher("command", publisher_);
        !IsOk(status)) return status;
    if (auto status = context.ResolveParameter("input_source", input_source_);
        !IsOk(status)) return status;
    if (auto status = context.ResolveHardware("device", device_);
        !IsOk(status)) return status;
    if (auto status = context.BindPeriodicTask("control", {Run, this});
        !IsOk(status)) return status;
    context_ = &context;
    return xrobot::runtime::Status::kOk;
  }

  xrobot::runtime::Status Start() noexcept override {
    if (context_ == nullptr || running_) {
      return xrobot::runtime::Status::kInvalidState;
    }
    running_ = true;
    return xrobot::runtime::Status::kOk;
  }

  void Shutdown() noexcept override { running_ = false; }

  inline static std::uint16_t last_value{};
  inline static std::uint32_t cycles{};

 private:
  static void Run(void* state,
                  const xrobot::runtime::ExecutionContext& execution) noexcept {
    auto& self = *static_cast<Source*>(state);
    if (!self.running_) return;
    ++cycles;
    last_value = static_cast<std::uint16_t>(
        self.input_source_->value() + self.device_->bias);
    self.publisher_.Publish({last_value, cycles}, self.context_->NowNs(),
                            execution);
  }

  std::string_view name_;
  xrobot::runtime::ModuleContext* context_{};
  xrobot::runtime::Parameter<std::uint32_t>* input_source_{};
  Device* device_{};
  xrobot::runtime::TopicPublisher<test::msg::Command> publisher_;
  bool running_{};
};

}  // namespace test
""",
    )
    write(
        tmp_path,
        "sink/include/test/sink.hpp",
        """\
#pragma once

#include <cstdint>
#include <string_view>

#include "robot_msgs/robot_msgs.hpp"
#include "xrobot/runtime/module.hpp"

namespace test {

class Sink final : public xrobot::runtime::Module {
 public:
  explicit Sink(std::string_view name) noexcept : name_(name) {}

  std::string_view Name() const noexcept override { return name_; }

  xrobot::runtime::Status Initialize(
      xrobot::runtime::ModuleContext& context) noexcept override {
    using xrobot::runtime::IsOk;
    xrobot::runtime::TopicSubscriber<test::msg::Command> subscriber;
    if (auto status = context.ResolveTopicSubscriber("command", subscriber);
        !IsOk(status)) return status;
    if (auto status = subscriber.Bind(Receive, this); !IsOk(status)) {
      return status;
    }
    return context.BindPeriodicTask("control", {Run, this});
  }

  xrobot::runtime::Status Start() noexcept override {
    running_ = true;
    return xrobot::runtime::Status::kOk;
  }

  void Shutdown() noexcept override { running_ = false; }

  inline static test::msg::Command last{};
  inline static std::uint32_t received{};

 private:
  static void Receive(
      void*, const test::msg::Command& command,
      const xrobot::runtime::MessageInfo&,
      const xrobot::runtime::ExecutionContext&) noexcept {
    last = command;
    ++received;
  }

  static void Run(void*,
                  const xrobot::runtime::ExecutionContext&) noexcept {}

  std::string_view name_;
  bool running_{};
};

}  // namespace test
""",
    )
    for package, kind in (("source", "publisher"), ("sink", "subscriber")):
        parameters = (
            """\
  parameters:
    - {name: enabled, type: bool, default: true, mutability: runtime, persistence: volatile}
    - {name: offset, type: int32, default: -2, minimum: -10, maximum: 10, mutability: build, persistence: compiled}
    - {name: input_source, type: uint32, default: 1, minimum: 0, maximum: 1, mutability: startup, persistence: compiled}
    - {name: gain, type: float32, default: 0.5, minimum: 0.0, maximum: 1.0, mutability: runtime, persistence: persistent}
    - {name: timeout_scale, type: float64, default: 1.0, minimum: 0.5, maximum: 2.0, mutability: startup, persistence: compiled}
"""
            if package == "source"
            else "  parameters: []\n"
        )
        header = f"test/{package}.hpp"
        hardware = (
            "  hardware: [device]" if package == "source" else "  hardware: []"
        )
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
  implementation: {{target: {package}, class: test::{package.title()}, header: {header}}}
  dependencies: []
  ports:
    - {{name: command, kind: {kind}, type: test.msg.Command, required: true}}
  executors:
    - {{name: control, priority: 4, stack_bytes: 1024, queue_depth: 4, period_us: 1000}}
{parameters.rstrip()}
{hardware}
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
    parameters: {input_source: 0, gain: 0.25}
    ports: {command: /control/command}
    hardware: {device: source_device}
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
    source_device: {{kind: test, backend: fake, resource: source0, options: {{}}}}
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
    authoritative_lock = tmp_path / "deployment.lock.yaml"

    first = compile_deployment(workspace, deployment, output, authoritative_lock)
    first_lock = (output / "deployment.lock.yaml").read_bytes()
    first_routes = (output / "reports/routes.json").read_bytes()
    first_composition_report = (output / "reports/composition.yaml").read_bytes()
    first_node_composition = (
        output / "nodes/node_a/node_composition.hpp"
    ).read_bytes()
    second = compile_deployment(workspace, deployment, output, authoritative_lock)

    assert first == second
    assert (output / "deployment.lock.yaml").read_bytes() == first_lock
    assert authoritative_lock.read_bytes() == first_lock
    assert (output / "reports/routes.json").read_bytes() == first_routes
    assert (
        output / "reports/composition.yaml"
    ).read_bytes() == first_composition_report
    assert (
        output / "nodes/node_a/node_composition.hpp"
    ).read_bytes() == first_node_composition
    lock = yaml.safe_load(first_lock)
    assert lock["nodes"] == {"node_a": 1, "node_b": 2}
    assert lock["routes"] == {"/control/command": 8}
    assert first.cross_node_route_count == 1
    assert first.node_count == 2
    assert (output / "nodes/node_a/node_descriptor.cpp").is_file()
    assert (output / "nodes/node_b/node_descriptor.cpp").is_file()
    assert not (output / "nodes/node_a/generated_main.cpp").exists()
    assert (output / "nodes/node_a/node_composition.hpp").is_file()
    assert (output / "nodes/node_b/node_composition.hpp").is_file()
    node_a_header = (output / "nodes/node_a/node_config.hpp").read_text()
    assert 'GeneratedModule{"command_source", "source", "source"' in node_a_header
    assert (
        'GeneratedExecutor{"command_source__control", "command_source", '
        '"control", 4, 1024, 4, 1000000ULL, false}' in node_a_header
    )
    assert "inline constexpr bool kConfigurationValid" in node_a_header
    assert "kBoolParameters" in node_a_header
    assert '"input_source"' in node_a_header
    assert "std::uint32_t{0U}" in node_a_header
    assert "std::bit_cast<float>(std::uint32_t{0x3e800000U})" in node_a_header
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
                str(node_dir / "node_descriptor.cpp"),
                "-o",
                str(node_dir / "node_descriptor.o"),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    composition = yaml.safe_load(
        (output / "reports/composition.yaml").read_text()
    )
    assert composition["nodes"] == {
        "node_a": {"ready": True, "blockers": []},
        "node_b": {"ready": True, "blockers": []},
    }

    generated_messages = tmp_path / "generated-messages"
    generate_interfaces(tmp_path / "robot-msgs/schemas", generated_messages)
    write(
        tmp_path,
        "composition_test.cpp",
        """\
#include <cassert>
#include <cstdint>

#include "node_a/node_composition.hpp"
#include "node_b/node_composition.hpp"
#include "xrobot/runtime/cooperative_executor.hpp"
#include "xrobot/runtime/hardware_registry.hpp"
#include "xrobot/runtime/port_registry.hpp"
#include "xrobot/runtime/topic.hpp"

namespace {

class Clock final : public xrobot::runtime::SteadyClock {
 public:
  std::uint64_t NowNs() const noexcept override { return now_ns; }
  std::uint64_t now_ns{1'000'000};
};

}  // namespace

int main() {
  using namespace xrobot::runtime;
  Clock clock;
  CooperativeExecutor<2> delivery("delivery", 3);
  StaticTopic<test::msg::Command, 1> topic("/control/command");
  TopicSubscription<test::msg::Command, 1> subscription(
      delivery, DeliveryPolicy::kLatest);
  StaticPortRegistry<1> source_ports;
  StaticPortRegistry<1> sink_ports;
  StaticHardwareRegistry<1> source_hardware;
  test::Device device;
  device.bias = 7;

  assert(delivery.Initialize() == Status::kOk);
  assert(delivery.Start() == Status::kOk);
  assert(topic.Connect(subscription) == Status::kOk);
  assert(topic.Seal() == Status::kOk);
  assert(source_ports.AddTopicPublisher("/control/command", topic) ==
         Status::kOk);
  assert(source_ports.Seal() == Status::kOk);
  assert(sink_ports.AddTopicSubscriber("/control/command", subscription) ==
         Status::kOk);
  assert(sink_ports.Seal() == Status::kOk);
  assert(source_hardware.Add("source_device", device) == Status::kOk);
  assert(source_hardware.Seal() == Status::kOk);

  xrobot::generated::node_a::NodeComposition source(clock);
  xrobot::generated::node_b::NodeComposition sink(clock);
  assert(source.Configure(source_ports, &source_hardware) == Status::kOk);
  assert(sink.Configure(sink_ports) == Status::kOk);
  assert(source.Initialize() == Status::kOk);
  assert(sink.Initialize() == Status::kOk);
  assert(source.Start() == Status::kOk);
  assert(sink.Start() == Status::kOk);
  assert(source.FindExecutor("command_source__control") != nullptr);
  assert(source.FindExecutor("missing") == nullptr);

  const ExecutionContext runtime_context(
      "runtime", ExecutionKind::kThread, 9);
  assert(source.Poll(clock.now_ns, runtime_context) == Status::kOk);
  assert(source.RunExecutor(0) == Status::kOk);
  assert(delivery.RunOne() == Status::kOk);
  assert(test::Source::cycles == 1);
  assert(test::Source::last_value == 7);
  assert(test::Sink::received == 1);
  assert(test::Sink::last.value == 7);
  assert(test::Sink::last.auxiliary == 1);
  assert(source.RunExecutor(99) == Status::kInvalidArgument);
  source.Shutdown();
  sink.Shutdown();
}
""",
    )
    repository_root = Path(__file__).parents[2]
    runtime = repository_root / "xrobot-runtime"
    composition_executable = tmp_path / "composition_test"
    subprocess.run(
        [
            "/usr/bin/c++",
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Wconversion",
            "-Wsign-conversion",
            "-Werror",
            "-I",
            str(output / "nodes"),
            "-I",
            str(generated_messages / "include"),
            "-I",
            str(runtime / "include"),
            "-I",
            str(tmp_path / "source/include"),
            "-I",
            str(tmp_path / "sink/include"),
            str(tmp_path / "composition_test.cpp"),
            str(runtime / "src/runtime.cpp"),
            "-o",
            str(composition_executable),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run([str(composition_executable)], check=True)
    budget = yaml.safe_load((output / "reports/link_budget.yaml").read_text())
    assert budget["links"]["robot_can"]["within_budget"] is True
    executors = yaml.safe_load((output / "reports/executors.yaml").read_text())
    assert executors["nodes"]["node_a"][0]["name"] == "command_source__control"
    module_graph = yaml.safe_load(
        (output / "reports/module_graph.json").read_text()
    )
    source_instance = next(
        item
        for item in module_graph["instances"]
        if item["name"] == "command_source"
    )
    assert source_instance["parameters"] == {
        "enabled": True,
        "gain": 0.25,
        "input_source": 0,
        "offset": -2,
        "timeout_scale": 1.0,
    }
    memory = yaml.safe_load((output / "reports/memory.yaml").read_text())
    assert memory["nodes"]["node_a"]["parameter_count"] == 5
    assert memory["nodes"]["node_a"]["parameter_value_bytes"] == 21
    routes = yaml.safe_load((output / "deployment.resolved.yaml").read_text())["routes"]
    assert routes[0]["max_serialized_size"] == 6
    assert routes[0]["max_wire_payload_size"] == 8
    assert routes[0]["frame_count"] == 2
    assert routes[0]["bits_per_message"] == 230


def test_rejects_an_instance_placed_more_than_once(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path)
    text = deployment.read_text(encoding="utf-8")
    deployment.write_text(
        text.replace("instances: [command_sink]", "instances: [command_source, command_sink]"),
        encoding="utf-8",
    )

    with pytest.raises(DeploymentError, match="command_source.*more than once"):
        compile_deployment(workspace, deployment, tmp_path / "generated")


def test_reports_composition_blockers_without_emitting_a_fake_entry(
    tmp_path: Path,
) -> None:
    workspace, deployment = create_workspace(tmp_path)
    source_manifest = tmp_path / "source/module.yaml"
    source_manifest.write_text(
        source_manifest.read_text(encoding="utf-8").replace(
            ", header: test/source.hpp", ""
        ),
        encoding="utf-8",
    )

    output = tmp_path / "generated"
    compile_deployment(workspace, deployment, output)
    report = yaml.safe_load((output / "reports/composition.yaml").read_text())

    assert report["nodes"]["node_a"]["ready"] is False
    assert report["nodes"]["node_a"]["blockers"] == [
        "instance command_source: implementation.header is not declared"
    ]
    assert not (output / "nodes/node_a/node_composition.hpp").exists()
    assert not (output / "nodes/node_a/generated_main.cpp").exists()
    assert (output / "nodes/node_a/node_descriptor.cpp").is_file()
    assert report["nodes"]["node_b"] == {"ready": True, "blockers": []}


def test_multiple_executors_require_one_explicit_default_for_composition(
    tmp_path: Path,
) -> None:
    workspace, deployment = create_workspace(tmp_path)
    source_manifest = tmp_path / "source/module.yaml"
    source_manifest.write_text(
        source_manifest.read_text(encoding="utf-8").replace(
            "    - {name: control, priority: 4, stack_bytes: 1024, queue_depth: 4, period_us: 1000}",
            "    - {name: control, priority: 4, stack_bytes: 1024, queue_depth: 4, period_us: 1000}\n"
            "    - {name: events, priority: 3, stack_bytes: 512, queue_depth: 2}",
        ),
        encoding="utf-8",
    )

    output = tmp_path / "generated"
    compile_deployment(workspace, deployment, output)
    report = yaml.safe_load((output / "reports/composition.yaml").read_text())

    assert report["nodes"]["node_a"]["blockers"] == [
        "instance command_source: multiple executors require exactly one default"
    ]
    assert not (output / "nodes/node_a/node_composition.hpp").exists()


def test_rejects_a_classic_can_budget_overflow(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path, max_rate_hz=10000)

    with pytest.raises(DeploymentError, match="robot_can.*utilization"):
        compile_deployment(workspace, deployment, tmp_path / "generated")


def test_classic_can_cost_includes_reliable_ack_and_operation_envelopes() -> None:
    assert _can_route_cost("service", [2, 4], 8) == (9, 5, 505)
    assert _can_route_cost("action", [2, 1, 2], 8) == (15, 15, 1525)

    with pytest.raises(DeploymentError, match="16 classic CAN fragments"):
        _can_route_cost("topic", [95], 8)


def test_package_lock_changes_the_whole_deployment_hash(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path)
    output = tmp_path / "generated"
    first = compile_deployment(workspace, deployment, output)
    package_lock = tmp_path / "package.lock.yaml"
    package_lock.write_text(
        package_lock.read_text(encoding="utf-8").replace(
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            "dddddddddddddddddddddddddddddddddddddddd",
        ),
        encoding="utf-8",
    )

    second = compile_deployment(workspace, deployment, output)

    assert first.deployment_hash != second.deployment_hash


def test_rejects_unknown_instance_parameters(tmp_path: Path) -> None:
    workspace, deployment = create_workspace(tmp_path)
    robot = tmp_path / "robot.yaml"
    robot.write_text(
        robot.read_text(encoding="utf-8").replace(
            "parameters: {input_source: 0, gain: 0.25}",
            "parameters: {input_source: 0, gain: 0.25, typo: 1}",
        ),
        encoding="utf-8",
    )

    with pytest.raises(DeploymentError, match="unknown parameters: typo"):
        compile_deployment(workspace, deployment, tmp_path / "generated")


@pytest.mark.parametrize(
    ("parameter_config", "message"),
    [
        ("{input_source: 2, gain: 0.25}", "input_source.*outside"),
        ("{input_source: true, gain: 0.25}", "input_source.*must be uint32"),
        ("{input_source: 0, gain: .nan}", "gain.*finite"),
    ],
)
def test_rejects_invalid_instance_parameter_values(
    tmp_path: Path, parameter_config: str, message: str
) -> None:
    workspace, deployment = create_workspace(tmp_path)
    robot = tmp_path / "robot.yaml"
    robot.write_text(
        robot.read_text(encoding="utf-8").replace(
            "{input_source: 0, gain: 0.25}", parameter_config
        ),
        encoding="utf-8",
    )

    with pytest.raises(DeploymentError, match=message):
        compile_deployment(workspace, deployment, tmp_path / "generated")
