from __future__ import annotations

from pathlib import Path


def write(root: Path, relative: str, content: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    return path


def create_workspace(root: Path) -> tuple[Path, Path, Path, Path]:
    write(
        root,
        "proto/state.proto",
        'syntax = "proto3";\n'
        "package test.v1;\n"
        "message State { uint32 value = 1; string label = 2; }\n",
    )
    write(
        root,
        "proto/bounds.yaml",
        "fields:\n  test.v1.State.label: {max_size: 8}\n",
    )
    for package, module, port in (
        ("sensors", "sensor", "publisher"),
        ("control", "controller", "subscriber"),
    ):
        platform = "zephyr" if package == "sensors" else "linux"
        write(
            root,
            f"{package}/package.yaml",
            f"""api_version: aster.dev/v1alpha2
kind: Package
metadata: {{name: {package}, version: 0.2.0, license: Apache-2.0}}
spec:
  build: {{system: cmake, target: {package}}}
  exports:
    modules: [module.yaml]
    protos: [../proto/state.proto]
  protobuf:
    bounds: ../proto/bounds.yaml
    includes: [../proto]
  dependencies: {{}}
""",
        )
        write(
            root,
            f"{package}/module.yaml",
            f"""api_version: aster.dev/v1alpha2
kind: Module
metadata: {{name: {module}, version: 0.2.0}}
spec:
  implementation: {{target: {package}, class: test::{module.title()}, header: test/{module}.hpp}}
  platforms: [{platform}]
  ports:
    - {{name: state, kind: {port}, type: test.v1.State, max_rate_hz: 100}}
  tasks:
    - name: loop
      domain: control
      period_us: 1000
      deadline_us: 1000
      stack_bytes: 1024
      queue_depth: 4
      priority: 10
""",
        )
        write(
            root,
            f"test/{module}.hpp",
            f"""#pragma once
#include "aster/module.hpp"

namespace test {{
class {module.title()} final : public aster::Module {{
 public:
  aster::ModuleInfo Info() const noexcept override {{
    return {{"{module}", "test", "{package}", {{0, 2, 0}}}};
  }}
  aster::Status Initialize(aster::CoreRef) noexcept override {{
    return aster::Status::kOk;
  }}
  aster::Status Start() noexcept override {{ return aster::Status::kOk; }}
  void Shutdown() noexcept override {{}}
}};
}}  // namespace test
""",
        )
    workspace = write(
        root,
        "workspace.yaml",
        """api_version: aster.dev/v1alpha2
kind: Workspace
metadata: {name: test-workspace}
spec:
  packages:
    control: {source: control}
    sensors: {source: sensors}
""",
    )
    application = write(
        root,
        "application.yaml",
        """api_version: aster.dev/v1alpha2
kind: Application
metadata: {name: robot}
spec:
  instances:
    imu: {module: sensors/sensor}
    controller: {module: control/controller}
  connections:
    - {from: imu.state, to: controller.state, qos: reliable, max_rate_hz: 100, max_size: 32}
  domains:
    - {name: control, time: monotonic}
""",
    )
    hardware = write(
        root,
        "dev_c.hardware.yaml",
        """api_version: aster.dev/v1alpha2
kind: Hardware
metadata: {name: dev-c-control}
spec:
  platform: zephyr
  board: dev_c/stm32f407xx
  resources:
    bus: {kind: can, backend: devicetree, device: can1, options: {}}
  memory: {flash_bytes: 1048576, ram_bytes: 131072}
""",
    )
    write(
        root,
        "linux.hardware.yaml",
        """api_version: aster.dev/v1alpha2
kind: Hardware
metadata: {name: linux-control}
spec:
  platform: linux
  resources:
    bus: {kind: can, backend: socketcan, device: can0, options: {}}
""",
    )
    deployment = write(
        root,
        "deployment.yaml",
        """api_version: aster.dev/v1alpha2
kind: Deployment
metadata: {name: robot-dev}
spec:
  application: application.yaml
  hosts:
    mcu: {os: zephyr, arch: arm, board: dev_c/stm32f407xx, hardware: dev_c.hardware.yaml}
    soc: {os: linux, arch: aarch64, hardware: linux.hardware.yaml}
  nodes:
    controller-node: {host: soc, instances: [controller], domains: [control]}
    sensor-node: {host: mcu, instances: [imu], domains: [control]}
  transports:
    can0: {type: can, hosts: [mcu, soc], bitrate_bps: 1000000, mtu: 8, resource: bus}
  time:
    authority: sensor-node
    domains:
      control: {source: monotonic}
  budgets:
    hosts:
      mcu: {stack_bytes: 8192}
      soc: {stack_bytes: 8192}
    transports: {can0: 0.8}
""",
    )
    return workspace, application, hardware, deployment
