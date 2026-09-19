"""Atomic minimal Linux project generation for aster init."""

from __future__ import annotations

import shutil
import tempfile
from pathlib import Path

from .validation import validate_document


class ProjectError(ValueError):
    pass


_FILES = {
    "README.md": """# AsterCtrl application

With AsterCtrl installed and its Python 3.12 aster_cli environment active:

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/asterctrl/install
    cmake --build build
    aster run --config build/runtime.yaml

For framework development from an AsterCtrl checkout, replace CMAKE_PREFIX_PATH with
-DASTERCTRL_SOURCE_DIR=/path/to/asterctrl and run with
--runtime build/asterctrl/aster_runtime.

Edit runtime.yaml to change the instance configuration, then rerun CMake to
copy it to build/runtime.yaml. No Module recompilation is needed.
The generated Package entry belongs in build/, not in application source.
The current Linux workflow uses runtime.yaml only; it has no custom launcher,
Application graph or Deployment Lock input.
Zephyr and cross-node deployment tooling is still being migrated to v1alpha3.
""",
    ".gitignore": "/build/\n",
    "CMakeLists.txt": """cmake_minimum_required(VERSION 3.28)
project(aster_app LANGUAGES CXX)

if(ASTERCTRL_SOURCE_DIR)
  set(ASTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ASTERCTRL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/asterctrl")
else()
  find_package(AsterCtrl CONFIG REQUIRED)
endif()

aster_add_package(demo MANIFEST package.yaml)
# Keep the filename portable across Linux and macOS development hosts.
set_target_properties(demo PROPERTIES PREFIX "" OUTPUT_NAME "demo" SUFFIX ".so")
configure_file(runtime.yaml runtime.yaml COPYONLY)
""",
    "package.yaml": """api_version: aster.dev/v1alpha3
kind: Package
metadata: {name: demo, version: 0.1.0, license: Apache-2.0}
spec:
  modules:
    - type: demo.Hello
      class: demo::Hello
      header: src/hello.hpp
      sources: [src/hello.cpp]
      platforms: [linux, zephyr]
""",
    "runtime.yaml": """api_version: aster.dev/v1alpha3
aster:
  packages:
    - {name: demo, path: ./demo.so}
  executors:
    - {name: main, type: serial, queue_capacity: 64}
  logging: {level: info}
  modules:
    - name: hello
      type: demo.Hello
      package: demo
      executor: main
      config: {message: hello from AsterCtrl}
""",
    "src/hello.hpp": """#pragma once

#include <aster_module_cpp_interface/module.hpp>

namespace demo {

class Hello final : public aster::ModuleBase {
 public:
  aster::ModuleInfo Info() const noexcept override;
  aster::Status Initialize(aster::CoreRef core) override;
  aster::Status Start() override;
  void Shutdown() noexcept override;

 private:
  aster::LoggerRef logger_;
  std::string_view message_;
};

}  // namespace demo
""",
    "src/hello.cpp": """#include "hello.hpp"

namespace demo {

aster::ModuleInfo Hello::Info() const noexcept {
  return {"hello", "demo.Hello", "demo", {0, 1, 0}};
}

aster::Status Hello::Initialize(aster::CoreRef core) {
  logger_ = core.logger();
  return core.configurator().Get("message", message_);
}

aster::Status Hello::Start() {
  return logger_.Write(aster::LogLevel::kInfo, message_);
}

void Hello::Shutdown() noexcept {}

}  // namespace demo
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
        validate_document(staging / "package.yaml")
        if target.exists():
            target.rmdir()
        staging.replace(target)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return target
