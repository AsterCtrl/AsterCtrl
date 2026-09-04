"""Atomic starter project generation for ``aster init``."""

from __future__ import annotations

import shutil
import tempfile
from pathlib import Path

from .graph import resolve_deployment
from .validation import validate_document


class ProjectError(ValueError):
    pass


_FILES = {
    "README.md": """# AsterCtrl application

Validate and resolve the simulation deployment:

```sh
aster validate workspace.yaml application.yaml deployment.sim.yaml
aster resolve workspace.yaml deployment.sim.yaml
aster codegen workspace.yaml deployment.sim.yaml build/generated
cmake -S . -B build/host -G Ninja \\
  -DASTER_GENERATED_DIR="$PWD/build/generated/nodes/app"
cmake --build build/host
./build/host/aster_app
```
""",
    "CMakeLists.txt": """cmake_minimum_required(VERSION 3.28)
project(aster_app LANGUAGES CXX)

if(ASTERCTRL_SOURCE_DIR)
  set(ASTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ASTERCTRL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/asterctrl"
                   EXCLUDE_FROM_ALL)
else()
  find_package(AsterCtrl 0.2 CONFIG REQUIRED)
endif()

if(NOT ASTER_GENERATED_DIR)
  message(FATAL_ERROR "run 'aster codegen' and set ASTER_GENERATED_DIR")
endif()

set(ASTER_MODULE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
include("${ASTER_GENERATED_DIR}/aster.generated.cmake")

add_executable(aster_app src/main.cpp)
target_compile_features(aster_app PRIVATE cxx_std_20)
target_include_directories(aster_app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${ASTER_GENERATED_DIR}")
target_link_libraries(aster_app PRIVATE aster_generated)
""",
    "workspace.yaml": """api_version: aster.dev/v1alpha2
kind: Workspace
metadata: {name: aster-app}
spec:
  packages:
    demo: {source: packages/demo}
""",
    "application.yaml": """api_version: aster.dev/v1alpha2
kind: Application
metadata: {name: aster-app}
spec:
  instances:
    source: {module: demo/source}
    sink: {module: demo/sink, startup_after: [source]}
  connections:
    - {from: source.state, to: sink.state, max_rate_hz: 10}
  domains:
    - {name: simulation, time: simulated}
""",
    "deployment.sim.yaml": """api_version: aster.dev/v1alpha2
kind: Deployment
metadata: {name: aster-app-sim}
spec:
  application: application.yaml
  hosts:
    local: {os: linux, arch: x86_64, inventory: local}
  nodes:
    app:
      id: 1
      host: local
      instances: [source, sink]
      domains: [simulation]
      executors:
        simulation: {policy: serial, stack_bytes: 2048, queue_depth: 4}
  time:
    authority: app
    domains:
      simulation: {source: simulated}
  budgets:
    hosts:
      local: {stack_bytes: 4096, ram_bytes: 65536}
""",
    "inventory.sim.yaml": """api_version: aster.dev/v1alpha2
kind: Inventory
metadata: {name: aster-app-sim}
spec:
  hosts:
    local: {transport: local, deploy_root: build/deploy}
""",
    "packages/demo/package.yaml": """api_version: aster.dev/v1alpha2
kind: Package
metadata: {name: demo, version: 0.1.0, license: Apache-2.0}
spec:
  build: {system: cmake, target: aster_app}
  exports:
    modules: [source.module.yaml, sink.module.yaml]
    protos: [proto/state.proto]
  protobuf:
    bounds: proto/bounds.yaml
    includes: [proto]
  dependencies: {}
""",
    "packages/demo/source.module.yaml": """api_version: aster.dev/v1alpha2
kind: Module
metadata: {name: source, version: 0.1.0}
spec:
  implementation:
    target: aster_app
    class: app::StateSource
    header: src/app_modules.hpp
  platforms: [linux]
  ports:
    - {name: state, kind: publisher, type: app.v1.State, max_rate_hz: 10}
  tasks:
    - name: publish
      domain: simulation
      period_us: 100000
      deadline_us: 100000
      stack_bytes: 1024
      queue_depth: 2
""",
    "packages/demo/sink.module.yaml": """api_version: aster.dev/v1alpha2
kind: Module
metadata: {name: sink, version: 0.1.0}
spec:
  implementation:
    target: aster_app
    class: app::StateSink
    header: src/app_modules.hpp
  platforms: [linux]
  ports:
    - {name: state, kind: subscriber, type: app.v1.State, max_rate_hz: 10}
  tasks:
    - {name: consume, domain: simulation, stack_bytes: 1024, queue_depth: 2}
""",
    "packages/demo/proto/state.proto": """syntax = "proto3";

package app.v1;

message State {
  uint32 value = 1;
}
""",
    "packages/demo/proto/bounds.yaml": """fields: {}
""",
    "src/app_modules.hpp": """#pragma once

#include <aster/channel.hpp>
#include <aster/module.hpp>

#include "state.pb.hpp"

namespace app {

class StateSource final : public aster::Module {
 public:
  aster::ModuleInfo Info() const noexcept override {
    return {"source", "app.StateSource", "demo", {0, 1, 0}};
  }
  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return publisher_.Bind(core.channel(), "source.state");
  }
  aster::Status Start() noexcept override {
    return publisher_.Publish(
        v1::State{42}, 1,
        aster::ExecutionContext{"main", aster::ExecutionKind::kThread, 1});
  }
  void Shutdown() noexcept override {}

 private:
  aster::Publisher<v1::State> publisher_;
};

class StateSink final : public aster::Module {
 public:
  aster::ModuleInfo Info() const noexcept override {
    return {"sink", "app.StateSink", "demo", {0, 1, 0}};
  }
  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return subscriber_.Bind(core.channel(), "source.state", Receive, this);
  }
  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}

 private:
  static aster::Status Receive(void*, const v1::State&,
                               const aster::MessageInfo&,
                               const aster::ExecutionContext&) noexcept {
    return aster::Status::kOk;
  }
  aster::Subscriber<v1::State> subscriber_;
};

}  // namespace app
""",
    "src/main.cpp": """#include <array>
#include <span>

#include <aster/channel.hpp>
#include <aster/runtime.hpp>

#include "composition.generated.hpp"

int main() {
  aster::LocalChannel<1, 1, 16> channel;
  auto handles = aster::CoreHandles{};
  handles.channel = aster::ChannelRef(channel);
  const aster::CoreRef core(handles);
  aster::generated::Composition composition(core);
  std::array<aster::RegistrySlot, 1> registries{{{&channel}}};
  aster::Runtime runtime(composition.Modules(), registries);
  if (!aster::IsOk(runtime.Initialize()) || !aster::IsOk(runtime.Start())) {
    return 1;
  }
  runtime.Shutdown();
  return runtime.state() == aster::RuntimeState::kStopped ? 0 : 2;
}
""",
}


def initialize_project(directory: str | Path) -> Path:
    target = Path(directory).resolve()
    if target.exists() and (not target.is_dir() or any(target.iterdir())):
        raise ProjectError(f"init target must be absent or empty: {target}")
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{target.name}.aster-init-", dir=target.parent))
    try:
        for relative, content in _FILES.items():
            destination = staging / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(content, encoding="utf-8", newline="\n")
        for name in (
            "workspace.yaml",
            "application.yaml",
            "deployment.sim.yaml",
            "inventory.sim.yaml",
            "packages/demo/package.yaml",
            "packages/demo/source.module.yaml",
            "packages/demo/sink.module.yaml",
        ):
            validate_document(staging / name)
        resolve_deployment(staging / "workspace.yaml", staging / "deployment.sim.yaml")
        if target.exists():
            target.rmdir()
        staging.replace(target)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return target
