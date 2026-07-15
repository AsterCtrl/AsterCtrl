"""Board-driven firmware entry points and CMake projects for MCU nodes."""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any

from xrobot_tools.deployment import DeploymentError, DeploymentPlan, Route
from xrobot_tools.hardware_codegen import hardware_build_requirements


_CPP_QUALIFIED_NAME = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*$"
)
_CPP_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_CAN_PRIORITY_BITS = {
    "control": 1,
    "state": 2,
    "event": 2,
    "background": 3,
}
_BOARD_IMPLEMENTATION_KEYS = {
    "target",
    "platform_target",
    "header",
    "class",
    "initialize",
    "entry",
    "system",
    "toolchain",
    "libxr_driver",
    "cmake_enable",
    "cmake_firmware",
}


def _board_implementation(plan: DeploymentPlan, node_name: str) -> dict[str, Any]:
    board = plan.boards[node_name]
    if board is None:
        raise DeploymentError(f"node {node_name!r} has no board export")
    return board.document["implementation"]


def _node_links(plan: DeploymentPlan, node_name: str) -> list[str]:
    return sorted(
        name
        for name, link in plan.deployment["links"].items()
        if link["transport"] == "xrobot-can"
        and any(endpoint["node"] == node_name for endpoint in link["endpoints"])
    )


def _node_instances(plan: DeploymentPlan, node_name: str) -> list[Any]:
    return sorted(
        (instance for instance in plan.instances if instance.node == node_name),
        key=lambda instance: instance.name,
    )


def firmware_blockers(plan: DeploymentPlan, node_name: str) -> list[str]:
    node = plan.deployment["nodes"][node_name]
    if node["runtime"] != "xrobot-mcu":
        return [f"runtime {node['runtime']!r} is not supported by the MCU generator"]
    board = plan.boards[node_name]
    if board is None:
        return [f"board export {node['target']['bsp']!r} is not available"]

    blockers: list[str] = []
    implementation = board.document.get("implementation")
    if not isinstance(implementation, dict):
        return ["board implementation metadata is not declared"]
    missing = sorted(_BOARD_IMPLEMENTATION_KEYS - set(implementation))
    if missing:
        blockers.append(
            "board implementation is missing " + ", ".join(missing)
        )
    for key in _BOARD_IMPLEMENTATION_KEYS & set(implementation):
        if not isinstance(implementation[key], str) or not implementation[key]:
            blockers.append(f"board implementation {key} must be a non-empty string")
    for key in ("class", "initialize"):
        value = implementation.get(key)
        if isinstance(value, str) and not _CPP_QUALIFIED_NAME.fullmatch(value):
            blockers.append(f"board implementation {key} is not a C++ qualified name")
    for key in ("entry", "cmake_enable", "cmake_firmware"):
        value = implementation.get(key)
        if isinstance(value, str) and not _CPP_IDENTIFIER.fullmatch(value):
            blockers.append(f"board implementation {key} is not an identifier")
    if implementation.get("system") != "freertos":
        blockers.append(
            f"board system {implementation.get('system')!r} is not supported"
        )

    toolchain = implementation.get("toolchain")
    if isinstance(toolchain, str) and not (board.package_path / toolchain).is_file():
        blockers.append(f"board toolchain {toolchain!r} does not exist")
    if not (board.package_path / "CMakeLists.txt").is_file():
        blockers.append("board package has no CMakeLists.txt")
    backend = plan.package_paths.get("xrobot-libxr-backend")
    if backend is None:
        blockers.append("workspace package xrobot-libxr-backend is not declared")
    elif not (backend / "CMakeLists.txt").is_file():
        blockers.append("xrobot-libxr-backend has no CMakeLists.txt")

    requirements = [
        (
            instance.package,
            instance.manifest.document["spec"]["implementation"].get("target"),
        )
        for instance in _node_instances(plan, node_name)
    ]
    requirements.extend(hardware_build_requirements(plan, node_name))
    for package, target in sorted(
        set(requirements), key=lambda item: (item[0], str(item[1]))
    ):
        package_path = plan.package_paths.get(package)
        if package_path is None:
            blockers.append(f"workspace package {package!r} is not declared")
        elif not (package_path / "CMakeLists.txt").is_file():
            blockers.append(f"package {package!r} has no CMakeLists.txt")
        if not isinstance(target, str) or not target:
            blockers.append(f"package {package!r} has no CMake target")

    for link_name in _node_links(plan, node_name):
        options = plan.deployment["links"][link_name]["options"]
        depth = options.get("receive_queue_depth", 32)
        if type(depth) is not int or not 1 <= depth <= 256:
            blockers.append(
                f"link {link_name!r} receive_queue_depth must be in [1, 256]"
            )
    return sorted(set(blockers))


def _route_arbitration_id(plan: DeploymentPlan, route: Route, route_id: int) -> int:
    qos = plan.deployment["qos_profiles"][route.qos]
    return (_CAN_PRIORITY_BITS[qos["class"]] << 9) | route_id


def _filter_ranges(
    plan: DeploymentPlan,
    node_name: str,
    link_name: str,
    route_ids: dict[str, int],
) -> list[tuple[int, int]]:
    identifiers = set(range(1, 8))
    for route in plan.routes:
        if route.link != link_name:
            continue
        if route.source_node != node_name and node_name not in route.destination_nodes:
            continue
        identifiers.add(_route_arbitration_id(plan, route, route_ids[route.name]))
    ordered = sorted(identifiers)
    ranges: list[tuple[int, int]] = []
    for identifier in ordered:
        if ranges and identifier == ranges[-1][1] + 1:
            ranges[-1] = (ranges[-1][0], identifier)
        else:
            ranges.append((identifier, identifier))
    return ranges


def render_firmware_entry(
    plan: DeploymentPlan, node_name: str, route_ids: dict[str, int]
) -> str:
    blockers = firmware_blockers(plan, node_name)
    if blockers:
        raise DeploymentError(
            f"node {node_name!r} firmware is not composable: " + "; ".join(blockers)
        )
    implementation = _board_implementation(plan, node_name)
    links = _node_links(plan, node_name)
    endpoint_resources: dict[str, str] = {}
    filter_declarations: list[str] = []
    endpoint_lines: list[str] = []
    adapter_lines: list[str] = []
    writer_entries: list[str] = []
    bind_lines: list[str] = []
    drain_lines: list[str] = []
    for index, link_name in enumerate(links):
        link = plan.deployment["links"][link_name]
        endpoint = next(
            endpoint for endpoint in link["endpoints"] if endpoint["node"] == node_name
        )
        endpoint_resources[link_name] = endpoint["resource"]
        filters = _filter_ranges(plan, node_name, link_name, route_ids)
        filter_entries = ",\n".join(
            "    xrobot::backend::libxr::CanFilterRange{"
            f"0x{first:03x}U, 0x{last:03x}U}}"
            for first, last in filters
        )
        filter_declarations.append(
            f"inline constexpr std::array<xrobot::backend::libxr::CanFilterRange, {len(filters)}> "
            f"kCanFilters{index}{{{{\n{filter_entries}\n}}}};"
        )
        queue_depth = int(link["options"].get("receive_queue_depth", 32))
        endpoint_lines.extend(
            [
                f"  auto* endpoint_{index} = hardware.CanEndpoint(\"{endpoint['resource']}\");",
                f"  if (endpoint_{index} == nullptr) {{",
                "    Halt(FirmwareStage::kCanEndpoint, Status::kUnavailable);",
                "  }",
            ]
        )
        adapter_lines.append(
            f"  static xrobot::backend::libxr::CanAdapter<{queue_depth}U> adapter_{index}{{"
            f"*endpoint_{index}, kCanFilters{index}}};"
        )
        writer_entries.append(
            "      xrobot::generated::CanLinkWriter{"
            f"\"{link_name}\", adapter_{index}.writer()}}"
        )
        bind_lines.extend(
            [
                f"  RequireOk(adapter_{index}.BindReceiver(",
                f"                composition.CanReceiver(\"{link_name}\")),",
                "            FirmwareStage::kCanBind);",
                f"  RequireOk(adapter_{index}.Initialize(), FirmwareStage::kCanInitialize);",
            ]
        )
        drain_lines.extend(
            [
                f"    CheckOperational(adapter_{index}.Drain(execution, {queue_depth}U),",
                "                     FirmwareStage::kCanDrain);",
            ]
        )

    if links:
        writers = (
            f"  const std::array<xrobot::generated::CanLinkWriter, {len(links)}> writers{{{{\n"
            + ",\n".join(writer_entries)
            + "\n  }};\n"
            "  static NodeComposition composition(\n"
            "      clock, std::span<const xrobot::generated::CanLinkWriter>{writers});"
        )
    else:
        writers = "  static NodeComposition composition(clock);"
    maximum_work = sum(
        int(executor["queue_depth"])
        for instance in _node_instances(plan, node_name)
        for executor in instance.manifest.document["spec"]["executors"]
    )

    return f"""\
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include \"{implementation['header']}\"
#include \"node_composition.hpp\"
#include \"node_hardware.hpp\"
#include \"FreeRTOS.h\"
#include \"task.h\"
#include \"xrobot/backend/libxr/can_adapter.hpp\"
#include \"xrobot/backend/libxr/steady_clock.hpp\"
#include \"xrobot/runtime/execution_context.hpp\"
#include \"xrobot/runtime/status.hpp\"

extern \"C\" {{
volatile std::uint32_t xrobot_firmware_fault_code = 0U;
}}

namespace xrobot::generated::{node_name} {{
namespace {{

using xrobot::runtime::Status;

enum class FirmwareStage : std::uint8_t {{
  kBoardInitialize = 1,
  kHardwareInitialize,
  kCanEndpoint,
  kCanBind,
  kCanInitialize,
  kCompositionConfigure,
  kCompositionInitialize,
  kBoardStart,
  kHardwareStart,
  kCanDrain,
  kCompositionPoll,
  kCompositionStart,
  kExecutorDrain,
  kHardwareExchange,
  kBoardPoll,
}};

{chr(10).join(filter_declarations)}

[[noreturn]] void Halt(FirmwareStage stage, Status status) noexcept {{
  xrobot_firmware_fault_code =
      (static_cast<std::uint32_t>(stage) << 8U) |
      static_cast<std::uint32_t>(status);
  for (;;) {{
    vTaskDelay(pdMS_TO_TICKS(100U));
  }}
}}

void RequireOk(Status status, FirmwareStage stage) noexcept {{
  if (status != Status::kOk) Halt(stage, status);
}}

void CheckOperational(Status status, FirmwareStage stage) noexcept {{
  if (status != Status::kOk && status != Status::kUnavailable &&
      status != Status::kCapacityExceeded && status != Status::kTimeout &&
      status != Status::kCancelled) {{
    Halt(stage, status);
  }}
}}

}}  // namespace
}}  // namespace xrobot::generated::{node_name}

extern \"C\" void {implementation['entry']}(void) {{
  using namespace xrobot::generated::{node_name};
  using xrobot::runtime::ExecutionContext;
  using xrobot::runtime::ExecutionKind;
  using xrobot::runtime::Status;

  {implementation['initialize']}();
  static xrobot::backend::libxr::SteadyClock clock;
  static {implementation['class']} board(clock);
  static NodeHardware hardware;
  const ExecutionContext execution(\"firmware_main\", ExecutionKind::kThread, 10U);

  RequireOk(board.Initialize(), FirmwareStage::kBoardInitialize);
  RequireOk(hardware.Initialize(board.hardware(), clock),
            FirmwareStage::kHardwareInitialize);
{chr(10).join(endpoint_lines)}
{chr(10).join(adapter_lines)}
{writers}
{chr(10).join(bind_lines)}
  RequireOk(composition.Configure(&hardware.hardware()),
            FirmwareStage::kCompositionConfigure);
  RequireOk(composition.Initialize(), FirmwareStage::kCompositionInitialize);
  RequireOk(board.Start(), FirmwareStage::kBoardStart);
  RequireOk(hardware.Start(execution), FirmwareStage::kHardwareStart);

  bool application_started{{}};
  for (;;) {{
    const auto now_ns = clock.NowNs();
{chr(10).join(drain_lines)}
    CheckOperational(composition.Poll(now_ns, execution),
                     FirmwareStage::kCompositionPoll);
    if (!application_started) {{
      const auto status = composition.Start();
      if (status == Status::kOk) {{
        application_started = true;
      }} else if (status != Status::kUnavailable) {{
        Halt(FirmwareStage::kCompositionStart, status);
      }}
    }}
    if (application_started) {{
      std::size_t executed{{}};
      CheckOperational(composition.DrainExecutors({maximum_work}U, executed),
                       FirmwareStage::kExecutorDrain);
    }}
    CheckOperational(hardware.Exchange(now_ns, execution),
                     FirmwareStage::kHardwareExchange);
    CheckOperational(board.Poll(execution), FirmwareStage::kBoardPoll);
    vTaskDelay(pdMS_TO_TICKS(1U));
  }}
}}
"""


def _relative_path(path: Path, base: Path) -> str:
    return Path(os.path.relpath(path, base)).as_posix()


def _cmake_package_path(plan: DeploymentPlan, package: str) -> str:
    relative = _relative_path(plan.package_paths[package], plan.workspace_root)
    if relative == ".":
        return "${XROBOT_WORKSPACE_ROOT}"
    return f"${{XROBOT_WORKSPACE_ROOT}}/{relative}"


def _test_option(package: str) -> str:
    if package == "motor":
        return "SRM_MOTOR_BUILD_TESTS"
    return re.sub(r"[^A-Za-z0-9]", "_", package).upper() + "_BUILD_TESTS"


def _build_requirements(
    plan: DeploymentPlan, node_name: str
) -> dict[str, set[str]]:
    requirements: dict[str, set[str]] = {}
    for instance in _node_instances(plan, node_name):
        implementation = instance.manifest.document["spec"]["implementation"]
        requirements.setdefault(instance.package, set()).add(implementation["target"])
    for package, target in hardware_build_requirements(plan, node_name):
        requirements.setdefault(package, set()).add(target)
    return requirements


def render_node_cmake(plan: DeploymentPlan, node_name: str, node_dir: Path) -> str:
    blockers = firmware_blockers(plan, node_name)
    if blockers:
        raise DeploymentError(
            f"node {node_name!r} firmware is not composable: " + "; ".join(blockers)
        )
    board = plan.boards[node_name]
    assert board is not None
    implementation = board.document["implementation"]
    workspace_relative = _relative_path(plan.workspace_root, node_dir)
    board_relative = _relative_path(board.package_path, plan.workspace_root)
    backend_path = _cmake_package_path(plan, "xrobot-libxr-backend")
    requirements = _build_requirements(plan, node_name)

    package_lines: list[str] = []
    for package in sorted(requirements):
        targets = sorted(requirements[package])
        package_lines.extend(
            [
                f"set({_test_option(package)} OFF CACHE BOOL \"\" FORCE)",
                f"if(NOT TARGET {targets[0]})",
                f"  add_subdirectory(\"{_cmake_package_path(plan, package)}\"",
                f"                   \"${{CMAKE_CURRENT_BINARY_DIR}}/packages/{package}\")",
                "endif()",
                "",
            ]
        )
    link_targets = sorted(
        {target for targets in requirements.values() for target in targets}
    )
    firmware_target = f"{node_name}_firmware"
    return f"""\
cmake_minimum_required(VERSION 3.22)

project({firmware_target} LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(_xrobot_default_workspace_root
  \"${{CMAKE_CURRENT_LIST_DIR}}/{workspace_relative}\" ABSOLUTE)
set(XROBOT_WORKSPACE_ROOT \"${{_xrobot_default_workspace_root}}\" CACHE PATH
    \"Workspace containing workspace.yaml\")
set(LIBXR_SYSTEM \"{implementation['system']}\" CACHE STRING \"\" FORCE)
set(LIBXR_DRIVER \"{implementation['libxr_driver']}\" CACHE STRING \"\" FORCE)
set(LIBXR_NO_EIGEN ON CACHE BOOL \"\" FORCE)
set(LIBXR_STATIC_BUILD ON CACHE BOOL \"\" FORCE)
set(XROBOT_RUNTIME_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)
set(XROBOT_TRANSPORTS_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)
set(XROBOT_LIBXR_BACKEND_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)

add_subdirectory(\"${{XROBOT_WORKSPACE_ROOT}}/{board_relative}\"
                 \"${{CMAKE_CURRENT_BINARY_DIR}}/board\")
add_subdirectory(\"{backend_path}\"
                 \"${{CMAKE_CURRENT_BINARY_DIR}}/xrobot-libxr-backend\")
target_link_libraries(xr PUBLIC {implementation['platform_target']})
{implementation['cmake_enable']}()

{chr(10).join(package_lines).rstrip()}

add_executable({firmware_target}
  firmware_entry.cpp
  node_descriptor.cpp)
target_include_directories({firmware_target} PRIVATE
  \"${{CMAKE_CURRENT_LIST_DIR}}\"
  \"${{CMAKE_CURRENT_LIST_DIR}}/..\")
target_link_libraries({firmware_target} PRIVATE
  {implementation['target']}
  {' '.join(link_targets)})
if(CMAKE_CXX_COMPILER_ID MATCHES \"Clang|GNU\")
  set_property(SOURCE firmware_entry.cpp node_descriptor.cpp APPEND PROPERTY
    COMPILE_OPTIONS
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()
{implementation['cmake_firmware']}({firmware_target})
"""


def render_cmake_presets(plan: DeploymentPlan, node_name: str, node_dir: Path) -> str:
    board = plan.boards[node_name]
    if board is None:
        raise DeploymentError(f"node {node_name!r} has no board export")
    implementation = board.document["implementation"]
    toolchain = _relative_path(
        board.package_path / implementation["toolchain"], node_dir
    )
    binary = _relative_path(
        plan.workspace_root / "build" / "firmware" / node_name, node_dir
    )
    profile = plan.deployment["nodes"][node_name]["target"]["profile"]
    build_type = "Release" if profile.endswith("release") else "Debug"
    document = {
        "version": 3,
        "configurePresets": [
            {
                "name": "firmware",
                "displayName": f"{node_name} {profile}",
                "generator": "Ninja",
                "binaryDir": "${sourceDir}/" + binary,
                "cacheVariables": {
                    "CMAKE_BUILD_TYPE": build_type,
                    "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/" + toolchain,
                },
            }
        ],
        "buildPresets": [
            {"name": "firmware", "configurePreset": "firmware"}
        ],
    }
    return json.dumps(document, indent=2, sort_keys=False) + "\n"
