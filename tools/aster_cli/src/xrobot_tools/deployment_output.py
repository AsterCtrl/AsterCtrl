"""Deterministic files emitted from a validated DeploymentPlan."""

from __future__ import annotations

import json
import re
import shutil
import struct
from pathlib import Path
from typing import Any

import yaml

from xrobot_tools.deployment import (
    DeploymentError,
    DeploymentPlan,
    Instance,
    ResolvedParameter,
    Route,
)
from xrobot_tools.validation import validate_document


def _write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8", newline="\n")


def _yaml(document: Any) -> str:
    return yaml.safe_dump(
        document,
        sort_keys=False,
        allow_unicode=False,
        default_flow_style=False,
    )


def _json(document: Any) -> str:
    return json.dumps(document, indent=2, sort_keys=True) + "\n"


def _cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


_PARAMETER_CPP_TYPES = {
    "bool": ("bool", "Bool"),
    "int32": ("std::int32_t", "Int32"),
    "uint32": ("std::uint32_t", "Uint32"),
    "float32": ("float", "Float32"),
    "float64": ("double", "Float64"),
}
_PARAMETER_MUTABILITY = {
    "build": "Build",
    "startup": "Startup",
    "runtime": "Runtime",
}
_PARAMETER_PERSISTENCE = {
    "compiled": "Compiled",
    "volatile": "Volatile",
    "persistent": "Persistent",
}
_RUNTIME_PARAMETER_MUTABILITY = {
    "build": "BuildTime",
    "startup": "Startup",
    "runtime": "Runtime",
}
_RUNTIME_PARAMETER_PERSISTENCE = {
    "compiled": "Compiled",
    "volatile": "Volatile",
    "persistent": "Persistent",
}
_CPP_QUALIFIED_NAME = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*$"
)


def _cpp_parameter_value(type_name: str, value: bool | int | float) -> str:
    cpp_type = _PARAMETER_CPP_TYPES[type_name][0]
    if type_name == "bool":
        return "true" if value else "false"
    if type_name == "int32":
        return f"{cpp_type}{{{value}}}"
    if type_name == "uint32":
        return f"{cpp_type}{{{value}U}}"
    if type_name == "float32":
        bits = struct.unpack("<I", struct.pack("<f", value))[0]
        return (
            "std::bit_cast<float>("
            f"std::uint32_t{{0x{bits:08x}U}})"
        )
    bits = struct.unpack("<Q", struct.pack("<d", value))[0]
    return (
        "std::bit_cast<double>("
        f"std::uint64_t{{0x{bits:016x}ULL}})"
    )


def _allocate_ids(
    names: list[str], existing: dict[str, int], minimum: int, maximum: int
) -> dict[str, int]:
    assigned = {
        name: value
        for name, value in existing.items()
        if name in names and minimum <= value <= maximum
    }
    used = set(assigned.values())
    candidate = minimum
    for name in sorted(names):
        if name in assigned:
            continue
        while candidate in used:
            candidate += 1
        if candidate > maximum:
            raise DeploymentError(f"cannot allocate ID for {name!r}; limit is {maximum}")
        assigned[name] = candidate
        used.add(candidate)
    return dict(sorted(assigned.items()))


def _load_existing_lock(path: Path, deployment_name: str) -> dict[str, Any]:
    if not path.is_file():
        return {}
    lock = validate_document(path)
    if lock["metadata"]["deployment"] != deployment_name:
        raise DeploymentError(
            f"{path}: lock belongs to deployment "
            f"{lock['metadata']['deployment']!r}, expected {deployment_name!r}"
        )
    return lock


def _route_document(route: Route, route_id: int) -> dict[str, Any]:
    return {
        "id": route_id,
        "name": route.name,
        "kind": route.kind,
        "type": route.type_name,
        "type_hash": route.type_hash,
        "source_node": route.source_node,
        "destination_nodes": list(route.destination_nodes),
        "link": route.link,
        "qos": route.qos,
        "max_rate_hz": route.max_rate_hz,
        "max_serialized_size": route.max_serialized_size,
        "max_wire_payload_size": route.max_wire_payload_size,
        "frame_count": route.frame_count,
        "bits_per_message": route.bits_per_message,
    }


def _node_header(
    plan: DeploymentPlan,
    node_name: str,
    node_id: int,
    route_ids: dict[str, int],
) -> str:
    routes = [
        route
        for route in plan.routes
        if route.source_node == node_name or node_name in route.destination_nodes
    ]
    namespace = f"xrobot::generated::{node_name}"
    route_entries = []
    for route in routes:
        direction = "true" if route.source_node == node_name else "false"
        route_entries.append(
            "    GeneratedRoute{"
            f"{route_ids[route.name]}, {_cpp_string(route.name)}, "
            f"{_cpp_string(route.type_name)}, "
            f"{direction}, {route.max_serialized_size}, "
            f"{route.max_wire_payload_size}, {route.frame_count}"
            "},"
        )
    route_lines = "\n".join(route_entries)

    instances = sorted(
        (instance for instance in plan.instances if instance.node == node_name),
        key=lambda item: item.name,
    )
    module_entries = []
    executor_entries = []
    parameter_entries: dict[str, list[str]] = {
        type_name: [] for type_name in _PARAMETER_CPP_TYPES
    }
    for instance in instances:
        implementation = instance.manifest.document["spec"]["implementation"]
        module_entries.append(
            "    GeneratedModule{"
            f"{_cpp_string(instance.name)}, {_cpp_string(instance.package)}, "
            f"{_cpp_string(instance.module)}, "
            f"{_cpp_string(implementation['target'])}, "
            f"{_cpp_string(implementation['class'])}"
            "},"
        )
        for executor in sorted(
            instance.manifest.document["spec"]["executors"],
            key=lambda item: item["name"],
        ):
            period_ns = int(executor.get("period_us", 0)) * 1000
            exclusive = "true" if executor.get("exclusive", False) else "false"
            executor_name = f"{instance.name}__{executor['name']}"
            executor_entries.append(
                "    GeneratedExecutor{"
                f"{_cpp_string(executor_name)}, "
                f"{_cpp_string(instance.name)}, {_cpp_string(executor['name'])}, "
                f"{executor['priority']}, {executor['stack_bytes']}, "
                f"{executor['queue_depth']}, {period_ns}ULL, {exclusive}"
                "},"
            )
        for parameter in instance.parameters:
            mutability = _PARAMETER_MUTABILITY[parameter.mutability]
            persistence = _PARAMETER_PERSISTENCE[parameter.persistence]
            cpp_type = _PARAMETER_CPP_TYPES[parameter.type_name][0]
            parameter_entries[parameter.type_name].append(
                f"    GeneratedParameter<{cpp_type}>{{"
                f"{_cpp_string(instance.name)}, "
                f"{_cpp_string(parameter.name)}, "
                f"{_cpp_string(parameter.unit)}, "
                f"{_cpp_parameter_value(parameter.type_name, parameter.value)}, "
                f"{_cpp_parameter_value(parameter.type_name, parameter.minimum)}, "
                f"{_cpp_parameter_value(parameter.type_name, parameter.maximum)}, "
                f"GeneratedParameterMutability::k{mutability}, "
                f"GeneratedParameterPersistence::k{persistence}"
                "},"
            )
    executor_entries.sort()
    module_lines = "\n".join(module_entries)
    executor_lines = "\n".join(executor_entries)
    parameter_tables = []
    parameter_table_names = []
    for type_name, (cpp_type, suffix) in _PARAMETER_CPP_TYPES.items():
        entries = sorted(parameter_entries[type_name])
        table_name = f"k{suffix}Parameters"
        parameter_table_names.append(table_name)
        parameter_tables.append(
            f"inline constexpr std::array<GeneratedParameter<{cpp_type}>, "
            f"{len(entries)}> {table_name}{{{{\n"
            + "\n".join(entries)
            + "\n}};"
        )
    parameter_table_lines = "\n".join(parameter_tables)
    parameter_validity = " &&\n      ".join(
        f"ParametersValid({name})" for name in parameter_table_names
    )
    return f"""\
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Generated by xrctl. Do not edit.
namespace {namespace} {{

struct GeneratedRoute {{
  std::uint16_t id;
  std::string_view name;
  std::string_view type;
  bool publishes;
  std::size_t max_serialized_size;
  std::size_t max_wire_payload_size;
  std::uint8_t frame_count;
}};

struct GeneratedModule {{
  std::string_view instance;
  std::string_view package;
  std::string_view module;
  std::string_view target;
  std::string_view class_name;
}};

struct GeneratedExecutor {{
  std::string_view name;
  std::string_view instance;
  std::string_view task;
  std::uint8_t priority;
  std::size_t stack_bytes;
  std::size_t queue_depth;
  std::uint64_t period_ns;
  bool exclusive;
}};

enum class GeneratedParameterMutability : std::uint8_t {{
  kBuild,
  kStartup,
  kRuntime,
}};

enum class GeneratedParameterPersistence : std::uint8_t {{
  kCompiled,
  kVolatile,
  kPersistent,
}};

template <typename Value>
struct GeneratedParameter {{
  std::string_view instance;
  std::string_view name;
  std::string_view unit;
  Value value;
  Value minimum;
  Value maximum;
  GeneratedParameterMutability mutability;
  GeneratedParameterPersistence persistence;
}};

inline constexpr std::uint8_t kNodeId = {node_id};
inline constexpr std::string_view kNodeName = "{node_name}";
inline constexpr std::string_view kDeploymentHash = "{plan.deployment_hash}";
inline constexpr std::string_view kSchemaHash = "{plan.schema_hash}";
inline constexpr std::array<GeneratedRoute, {len(routes)}> kRoutes{{{{
{route_lines}
}}}};
inline constexpr std::array<GeneratedModule, {len(instances)}> kModules{{{{
{module_lines}
}}}};
inline constexpr std::array<GeneratedExecutor, {len(executor_entries)}> kExecutors{{{{
{executor_lines}
}}}};
{parameter_table_lines}

template <typename Value, std::size_t Count>
consteval bool ParametersValid(
    const std::array<GeneratedParameter<Value>, Count>& parameters) {{
  for (std::size_t index = 0; index < parameters.size(); ++index) {{
    const auto& parameter = parameters[index];
    if (parameter.instance.empty() || parameter.name.empty() ||
        parameter.value < parameter.minimum ||
        parameter.value > parameter.maximum) {{
      return false;
    }}
    bool instance_found = false;
    for (const auto& module : kModules) {{
      if (module.instance == parameter.instance) {{
        instance_found = true;
        break;
      }}
    }}
    if (!instance_found) {{
      return false;
    }}
    for (std::size_t previous = 0; previous < index; ++previous) {{
      if (parameters[previous].instance == parameter.instance &&
          parameters[previous].name == parameter.name) {{
        return false;
      }}
    }}
  }}
  return true;
}}

consteval bool ConfigurationValid() {{
  for (std::size_t index = 0; index < kExecutors.size(); ++index) {{
    const auto& executor = kExecutors[index];
    if (executor.name.empty() || executor.instance.empty() ||
        executor.task.empty() || executor.stack_bytes == 0 ||
        executor.queue_depth == 0) {{
      return false;
    }}
    bool instance_found = false;
    for (const auto& module : kModules) {{
      if (module.instance == executor.instance) {{
        instance_found = true;
        break;
      }}
    }}
    if (!instance_found) {{
      return false;
    }}
    for (std::size_t previous = 0; previous < index; ++previous) {{
      if (kExecutors[previous].name == executor.name) {{
        return false;
      }}
    }}
  }}
  return {parameter_validity};
}}

inline constexpr bool kConfigurationValid = ConfigurationValid();

}}  // namespace {namespace}
"""


def _node_descriptor(node_name: str) -> str:
    namespace = f"xrobot::generated::{node_name}"
    return f"""\
#include "node_config.hpp"

// Descriptor-only compile check. This is not a firmware entry point.
namespace {namespace} {{

int ValidateNodeDescriptor() noexcept {{
  static_assert(kNodeId != 0);
  static_assert(kConfigurationValid);
  return 0;
}}

}}  // namespace {namespace}
"""


def _can_link_header() -> str:
    return """\
#pragma once

#include <string_view>

#include "xrobot/transport/can/link.hpp"

// Generated by xrctl. Do not edit.
namespace xrobot::generated {

struct CanLinkWriter {
  std::string_view link;
  xrobot::transport::can::CanFrameWriter writer;
};

}  // namespace xrobot::generated
"""


def _cpp_type_name(type_name: str) -> str:
    return "::" + type_name.replace(".", "::")


def _node_can_links(plan: DeploymentPlan, node_name: str) -> list[str]:
    return sorted(
        name
        for name, link in plan.deployment["links"].items()
        if link["transport"] == "xrobot-can"
        and any(endpoint["node"] == node_name for endpoint in link["endpoints"])
    )


def _node_topic_specs(
    plan: DeploymentPlan, node_name: str, route_ids: dict[str, int]
) -> list[dict[str, Any]]:
    routes = {route.name: route for route in plan.routes}
    specs: list[dict[str, Any]] = []
    for name, endpoints in plan.bindings.items():
        if not {endpoint.kind for endpoint in endpoints} <= {
            "publisher",
            "subscriber",
        }:
            continue
        local = [endpoint for endpoint in endpoints if endpoint.node == node_name]
        if not local:
            continue
        route = routes.get(name)
        local_subscribers = sum(
            endpoint.kind == "subscriber" for endpoint in local
        )
        outgoing = route is not None and route.source_node == node_name
        incoming = route is not None and node_name in route.destination_nodes
        if route is None:
            qos = {
                "delivery": "latest",
                "history_depth": 1,
                "class": "state",
                "deadline_ms": 0,
                "max_age_ms": 0,
                "rearm": "fresh_sample",
            }
        else:
            qos = plan.deployment["qos_profiles"][route.qos]
        specs.append(
            {
                "name": name,
                "type": endpoints[0].type_name,
                "publisher": any(endpoint.kind == "publisher" for endpoint in local),
                "subscriber": local_subscribers > 0,
                "sink_capacity": max(1, local_subscribers + int(outgoing)),
                "depth": int(qos["history_depth"]),
                "delivery": qos["delivery"],
                "outgoing": outgoing,
                "incoming": incoming,
                "route": route,
                "route_id": route_ids[name] if route is not None else None,
                "qos": qos,
            }
        )
    return specs


_CAN_PRIORITIES = {
    "control": "kControl",
    "state": "kState",
    "event": "kState",
    "background": "kBackground",
}
_REARM_POLICIES = {
    "automatic": "kAutomatic",
    "fresh_sample": "kFreshSample",
    "explicit": "kExplicit",
}


def _duration_ns(value_ms: Any) -> int:
    return int(round(float(value_ms) * 1_000_000))


def _node_ports(
    plan: DeploymentPlan, node_name: str, route_ids: dict[str, int]
) -> str:
    namespace = f"xrobot::generated::{node_name}"
    links = _node_can_links(plan, node_name)
    topics = _node_topic_specs(plan, node_name, route_ids)
    registry_entries = sum(
        int(topic["publisher"]) + int(topic["subscriber"]) for topic in topics
    )

    writer_initializers = [
        f"        writer_{index}_(FindWriter(writers, {_cpp_string(link)}))"
        for index, link in enumerate(links)
    ]
    writer_validity = " &&\n        ".join(
        f"HasOneWriter(writers, {_cpp_string(link)})" for link in links
    )
    if writer_initializers:
        constructor = (
            "  explicit NodePorts(std::span<const CanLinkWriter> writers) noexcept\n"
            "      :\n"
            + ",\n".join(writer_initializers)
            + ",\n"
            f"        writers_valid_({writer_validity}) {{}}"
        )
    else:
        constructor = (
            "  explicit NodePorts(std::span<const CanLinkWriter> writers) noexcept {\n"
            "    (void)writers;\n"
            "  }"
        )

    configure_lines = [
        "  xrobot::runtime::Status Configure() noexcept {",
        "    using xrobot::runtime::IsOk;",
        "    using xrobot::runtime::Status;",
        "    if (configuration_attempted_) return Status::kInvalidState;",
        "    configuration_attempted_ = true;",
        "    if (!writers_valid_) return Status::kInvalidArgument;",
    ]
    for index, topic in enumerate(topics):
        if topic["outgoing"]:
            configure_lines.extend(
                [
                    f"    if (const auto status = topic_{index}_.Connect(egress_{index}_);",
                    "        !IsOk(status)) return status;",
                ]
            )
        if topic["publisher"]:
            configure_lines.extend(
                [
                    "    if (const auto status = ports_.AddTopicPublisher(",
                    f"            {_cpp_string(topic['name'])}, topic_{index}_);",
                    "        !IsOk(status)) return status;",
                ]
            )
        if topic["subscriber"]:
            configure_lines.extend(
                [
                    "    if (const auto status = ports_.AddTopicSubscriber(",
                    f"            {_cpp_string(topic['name'])}, topic_{index}_);",
                    "        !IsOk(status)) return status;",
                ]
            )
    configure_lines.extend(
        [
            "    if (const auto status = ports_.Seal(); !IsOk(status)) return status;",
            "    configured_ = true;",
            "    return Status::kOk;",
            "  }",
            "",
            "  xrobot::runtime::Status Seal() noexcept {",
            "    using xrobot::runtime::IsOk;",
            "    using xrobot::runtime::Status;",
            "    if (!configured_ || sealed_) return Status::kInvalidState;",
        ]
    )
    for index, _ in enumerate(topics):
        configure_lines.extend(
            [
                f"    if (const auto status = topic_{index}_.Seal();",
                "        !IsOk(status)) return status;",
            ]
        )
    configure_lines.extend(
        [
            "    sealed_ = true;",
            "    return Status::kOk;",
            "  }",
            "",
            "  xrobot::runtime::PortResolver& resolver() noexcept { return ports_; }",
            "",
            "  xrobot::transport::can::CanFrameReceiver CanReceiver(",
            "      std::string_view link) noexcept {",
        ]
    )
    for index, link in enumerate(links):
        configure_lines.append(
            f"    if (link == {_cpp_string(link)}) return {{Receive{index}, this}};"
        )
    configure_lines.extend(
        [
            "    return {};",
            "  }",
            "",
            " private:",
            "  static xrobot::transport::can::CanFrameWriter FindWriter(",
            "      std::span<const CanLinkWriter> writers,",
            "      std::string_view link) noexcept {",
            "    for (const auto& candidate : writers) {",
            "      if (candidate.link == link) return candidate.writer;",
            "    }",
            "    return {};",
            "  }",
            "",
            "  static bool HasOneWriter(std::span<const CanLinkWriter> writers,",
            "                           std::string_view link) noexcept {",
            "    std::size_t count{};",
            "    bool callable{};",
            "    for (const auto& candidate : writers) {",
            "      if (candidate.link == link) {",
            "        ++count;",
            "        callable = candidate.writer.write != nullptr;",
            "      }",
            "    }",
            "    return count == 1 && callable;",
            "  }",
            "",
        ]
    )

    dispatch_lines: list[str] = []
    for link_index, link in enumerate(links):
        dispatch_lines.extend(
            [
                f"  static xrobot::runtime::Status Receive{link_index}(",
                "      void* state, const xrobot::transport::can::CanFrame& frame,",
                "      std::uint64_t receive_time_ns,",
                "      const xrobot::runtime::ExecutionContext& caller) noexcept {",
                f"    return static_cast<NodePorts*>(state)->Dispatch{link_index}(",
                "        frame, receive_time_ns, caller);",
                "  }",
                "",
                f"  xrobot::runtime::Status Dispatch{link_index}(",
                "      const xrobot::transport::can::CanFrame& frame,",
                "      std::uint64_t receive_time_ns,",
                "      const xrobot::runtime::ExecutionContext& caller) noexcept {",
                "    using xrobot::runtime::Status;",
                "    using namespace xrobot::transport::can;",
                "    (void)receive_time_ns;",
                "    (void)caller;",
                "    if (!sealed_) return Status::kInvalidState;",
                "    const auto id = CanArbitrationId::Decode(frame.arbitration_id);",
                "    if (!id.has_value()) return Status::kInvalidArgument;",
                "    switch (id->route_id) {",
            ]
        )
        for topic_index, topic in enumerate(topics):
            route = topic["route"]
            if not topic["incoming"] or route.link != link:
                continue
            priority = _CAN_PRIORITIES[topic["qos"]["class"]]
            dispatch_lines.extend(
                [
                    f"      case {topic['route_id']}:",
                    f"        if (id->priority != CanPriority::{priority})",
                    "          return Status::kInvalidArgument;",
                    f"        return ingress_{topic_index}_.Accept(",
                    "            frame, receive_time_ns, caller);",
                ]
            )
        dispatch_lines.extend(
            [
                "      default: return Status::kUnavailable;",
                "    }",
                "  }",
                "",
            ]
        )

    field_lines: list[str] = []
    for index, _ in enumerate(links):
        field_lines.append(
            f"  xrobot::transport::can::CanFrameWriter writer_{index}_;"
        )
    for index, topic in enumerate(topics):
        cpp_type = _cpp_type_name(topic["type"])
        delivery = "kLatest" if topic["delivery"] == "latest" else "kKeepAll"
        field_lines.extend(
            [
                f"  xrobot::runtime::StaticTopicChannel<{cpp_type}, ",
                f"      {topic['sink_capacity']}, {topic['depth']}> topic_{index}_{{",
                f"          {_cpp_string(topic['name'])}, xrobot::runtime::DeliveryPolicy::{delivery}}};",
            ]
        )
        if topic["outgoing"]:
            route = topic["route"]
            link_index = links.index(route.link)
            priority = _CAN_PRIORITIES[topic["qos"]["class"]]
            field_lines.extend(
                [
                    f"  xrobot::transport::can::FastTopicEgress<{cpp_type}> egress_{index}_{{",
                    f"      {topic['route_id']}, xrobot::transport::can::CanPriority::{priority},",
                    f"      writer_{link_index}_}};",
                ]
            )
        if topic["incoming"]:
            deadline_ns = _duration_ns(topic["qos"].get("deadline_ms", 0))
            max_age_ns = _duration_ns(topic["qos"].get("max_age_ms", 0))
            rearm = _REARM_POLICIES[topic["qos"].get("rearm", "fresh_sample")]
            field_lines.extend(
                [
                    f"  xrobot::transport::can::FastTopicIngress<{cpp_type}> ingress_{index}_{{",
                    f"      {topic['route_id']}, topic_{index}_.publisher(),",
                    "      xrobot::transport::can::FreshnessConfig{",
                    f"          {deadline_ns}ULL, {max_age_ns}ULL,",
                    f"          xrobot::transport::can::RearmPolicy::{rearm}}}}};",
                ]
            )
    field_lines.extend(
        [
            f"  xrobot::runtime::StaticPortRegistry<{max(1, registry_entries)}> ports_;",
            f"  bool writers_valid_{{{str(not links).lower()}}};",
            "  bool configuration_attempted_{};",
            "  bool configured_{};",
            "  bool sealed_{};",
            "};",
        ]
    )

    return f"""\
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "can_link.hpp"
#include "robot_msgs/robot_msgs.hpp"
#include "xrobot/runtime/port_registry.hpp"
#include "xrobot/runtime/topic.hpp"
#include "xrobot/transport/can/protocol.hpp"
#include "xrobot/transport/can/topic_bridge.hpp"

// Generated by xrctl. Do not edit.
namespace {namespace} {{

class NodePorts {{
 public:
{constructor}

{chr(10).join(configure_lines)}
{chr(10).join(dispatch_lines)}
{chr(10).join(field_lines)}

}}  // namespace {namespace}
"""


def _node_instances(plan: DeploymentPlan, node_name: str) -> list[Instance]:
    return sorted(
        (instance for instance in plan.instances if instance.node == node_name),
        key=lambda item: item.name,
    )


def _composition_blockers(plan: DeploymentPlan, node_name: str) -> list[str]:
    blockers: list[str] = []
    for instance in _node_instances(plan, node_name):
        implementation = instance.manifest.document["spec"]["implementation"]
        header = implementation.get("header")
        if header is None:
            blockers.append(
                f"instance {instance.name}: implementation.header is not declared"
            )
        elif not (instance.manifest.package_path / "include" / header).is_file():
            blockers.append(
                f"instance {instance.name}: include/{header} does not exist"
            )
        if not _CPP_QUALIFIED_NAME.fullmatch(implementation["class"]):
            blockers.append(
                f"instance {instance.name}: implementation.class is not a C++ qualified name"
            )

        executors = instance.manifest.document["spec"]["executors"]
        executor_names = [item["name"] for item in executors]
        defaults = [item for item in executors if item.get("default", False)]
        if not executors:
            blockers.append(f"instance {instance.name}: no executor is declared")
        elif len(executor_names) != len(set(executor_names)):
            blockers.append(
                f"instance {instance.name}: executor names are not unique"
            )
        elif len(executors) > 1 and len(defaults) != 1:
            blockers.append(
                f"instance {instance.name}: multiple executors require exactly one default"
            )
    return blockers


def _default_executor(instance: Instance) -> dict[str, Any]:
    executors = instance.manifest.document["spec"]["executors"]
    if len(executors) == 1:
        return executors[0]
    return next(item for item in executors if item.get("default", False))


def _parameter_declaration(
    instance_index: int,
    parameter_index: int,
    parameter: ResolvedParameter,
) -> str:
    cpp_type = _PARAMETER_CPP_TYPES[parameter.type_name][0]
    mutability = _RUNTIME_PARAMETER_MUTABILITY[parameter.mutability]
    persistence = _RUNTIME_PARAMETER_PERSISTENCE[parameter.persistence]
    return (
        f"  xrobot::runtime::Parameter<{cpp_type}> "
        f"parameter_{instance_index}_{parameter_index}_{{\n"
        f"      xrobot::runtime::ParameterDescriptor<{cpp_type}>{{"
        f"{_cpp_string(parameter.name)}, {_cpp_string(parameter.unit)}, "
        f"{_cpp_parameter_value(parameter.type_name, parameter.value)}, "
        f"{_cpp_parameter_value(parameter.type_name, parameter.minimum)}, "
        f"{_cpp_parameter_value(parameter.type_name, parameter.maximum)}, "
        f"xrobot::runtime::ParameterMutability::k{mutability}, "
        f"xrobot::runtime::ParameterPersistence::k{persistence}}}}};"
    )


def _node_composition(plan: DeploymentPlan, node_name: str) -> str:
    instances = _node_instances(plan, node_name)
    namespace = f"xrobot::generated::{node_name}"
    headers = sorted(
        {
            instance.manifest.document["spec"]["implementation"]["header"]
            for instance in instances
        }
    )
    include_lines = "\n".join(f'#include "{header}"' for header in headers)

    executor_specs: list[tuple[Instance, dict[str, Any], str]] = []
    executor_fields: dict[tuple[str, str], str] = {}
    for instance in instances:
        for executor in sorted(
            instance.manifest.document["spec"]["executors"],
            key=lambda item: item["name"],
        ):
            field = f"executor_{len(executor_specs)}_"
            full_name = f"{instance.name}__{executor['name']}"
            executor_fields[(instance.name, executor["name"])] = field
            executor_specs.append((instance, executor, full_name))

    periodic_specs = [
        item for item in executor_specs if "period_us" in item[1]
    ]
    drain_order = sorted(
        range(len(executor_specs)),
        key=lambda index: (
            -int(executor_specs[index][1]["priority"]),
            executor_specs[index][2],
        ),
    )
    has_hardware = any(instance.config.get("hardware") for instance in instances)

    public_lines = [
        "class NodeComposition {",
        " public:",
        "  explicit NodeComposition(",
        "      xrobot::runtime::SteadyClock& clock,",
        "      xrobot::runtime::LogSink* log = nullptr,",
        "      xrobot::runtime::DiagnosticSink* diagnostics = nullptr) noexcept",
        "      : NodeComposition(clock, std::span<const CanLinkWriter>{}, log,",
        "                        diagnostics) {}",
        "",
        "  NodeComposition(",
        "      xrobot::runtime::SteadyClock& clock,",
        "      std::span<const CanLinkWriter> links,",
        "      xrobot::runtime::LogSink* log = nullptr,",
        "      xrobot::runtime::DiagnosticSink* diagnostics = nullptr) noexcept",
        "      :",
    ]
    context_initializers = ["        ports_(links)"]
    for index, instance in enumerate(instances):
        default_executor = _default_executor(instance)
        executor_field = executor_fields[(instance.name, default_executor["name"])]
        ports = f"&ports_{index}_" if instance.config.get("ports") else "nullptr"
        parameters = f"&parameters_{index}_" if instance.parameters else "nullptr"
        periodic = (
            "&scheduler_"
            if any(item[0].name == instance.name for item in periodic_specs)
            else "nullptr"
        )
        hardware = (
            f"&hardware_{index}_" if instance.config.get("hardware") else "nullptr"
        )
        context_initializers.append(
            f"        context_{index}_(kNodeName, {_cpp_string(instance.name)},\n"
            "                   xrobot::runtime::ModuleServices{"
            f".executor = &{executor_field}, .clock = &clock, .log = log, "
            f".diagnostics = diagnostics, .ports = {ports}, "
            f".parameters = {parameters}, .periodic_tasks = {periodic}, "
            f".hardware = {hardware}}})"
        )
    runtime_initializer = (
        "        runtime_(executor_slots_, module_slots_, scheduler_)"
        if periodic_specs
        else "        runtime_(executor_slots_, module_slots_)"
    )
    public_lines.append(",\n".join([*context_initializers, runtime_initializer]) + " {}")
    public_lines.extend(
        [
            "",
            "  xrobot::runtime::Status Configure(",
            "      xrobot::runtime::HardwareResolver* hardware = nullptr) noexcept {",
            "    using xrobot::runtime::IsOk;",
            "    if (const auto status = ports_.Configure(); !IsOk(status)) return status;",
            "    seal_generated_ports_ = true;",
            "    return Configure(ports_.resolver(), hardware);",
            "  }",
            "",
            "  xrobot::runtime::Status Configure(",
            "      xrobot::runtime::PortResolver& ports,",
            "      xrobot::runtime::HardwareResolver* hardware = nullptr) noexcept {",
            "    using xrobot::runtime::IsOk;",
            "    using xrobot::runtime::Status;",
            "    if (configuration_attempted_) return Status::kInvalidState;",
        ]
    )
    if has_hardware:
        public_lines.append(
            "    if (hardware == nullptr) return Status::kInvalidArgument;"
        )
    else:
        public_lines.append("    (void)hardware;")
    public_lines.append("    configuration_attempted_ = true;")
    for index, instance in enumerate(instances):
        if instance.config.get("ports"):
            public_lines.extend(
                [
                    f"    if (const auto status = ports_{index}_.Bind(ports);",
                    "        !IsOk(status)) return status;",
                ]
            )
        if instance.config.get("hardware"):
            public_lines.extend(
                [
                    f"    if (const auto status = hardware_{index}_.Bind(*hardware);",
                    "        !IsOk(status)) return status;",
                ]
            )
        for parameter_index, _ in enumerate(instance.parameters):
            public_lines.append(
                f"    parameter_{index}_{parameter_index}_.SealStartup();"
            )
            public_lines.extend(
                [
                    "    if (const auto status =",
                    f"            parameters_{index}_.Add(parameter_{index}_{parameter_index}_);",
                    "        !IsOk(status)) return status;",
                ]
            )
        if instance.parameters:
            public_lines.extend(
                [
                    f"    if (const auto status = parameters_{index}_.Seal();",
                    "        !IsOk(status)) return status;",
                ]
            )
    for instance, executor, _ in periodic_specs:
        field = executor_fields[(instance.name, executor["name"])]
        public_lines.extend(
            [
                "    if (const auto status = scheduler_.AddTask(",
                f"            {_cpp_string(instance.name)}, {_cpp_string(executor['name'])},",
                f"            {int(executor['period_us']) * 1000}ULL, {field});",
                "        !IsOk(status)) return status;",
            ]
        )
    public_lines.extend(
        [
            "    configured_ = true;",
            "    return Status::kOk;",
            "  }",
            "",
            "  xrobot::runtime::Status Initialize() noexcept {",
            "    using xrobot::runtime::IsOk;",
            "    using xrobot::runtime::Status;",
            "    if (!configured_) return Status::kInvalidState;",
            "    const auto status = runtime_.Initialize();",
            "    if (!IsOk(status) || !seal_generated_ports_) return status;",
            "    return ports_.Seal();",
            "  }",
            "  xrobot::runtime::Status Start() noexcept { return runtime_.Start(); }",
            "  xrobot::runtime::Status Poll(",
            "      std::uint64_t now_ns,",
            "      const xrobot::runtime::ExecutionContext& caller) noexcept {",
            "    return runtime_.Poll(now_ns, caller);",
            "  }",
            "  void Shutdown() noexcept { runtime_.Shutdown(); }",
            "",
            f"  static constexpr std::size_t executor_count() noexcept {{ return {len(executor_specs)}; }}",
            "  xrobot::runtime::Executor* FindExecutor(std::string_view name) noexcept {",
        ]
    )
    for index, (_, _, full_name) in enumerate(executor_specs):
        public_lines.append(
            f"    if (name == {_cpp_string(full_name)}) return &executor_{index}_;"
        )
    public_lines.extend(
        [
            "    return nullptr;",
            "  }",
            "  xrobot::runtime::Status RunExecutor(std::size_t index) noexcept {",
            "    switch (index) {",
        ]
    )
    for index, _ in enumerate(executor_specs):
        public_lines.append(
            f"      case {index}: return executor_{index}_.RunOne();"
        )
    public_lines.extend(
        [
            "      default: return xrobot::runtime::Status::kInvalidArgument;",
            "    }",
            "  }",
            "  xrobot::runtime::Status DrainExecutors(",
            "      std::size_t maximum_work, std::size_t& executed) noexcept {",
            "    using xrobot::runtime::Status;",
            "    executed = 0;",
            "    if (maximum_work == 0) return Status::kInvalidArgument;",
            "    while (executed < maximum_work) {",
            "      bool progressed{};",
        ]
    )
    for index in drain_order:
        public_lines.extend(
            [
                f"      if (const auto status = executor_{index}_.RunOne();",
                "          status == Status::kOk) {",
                "        ++executed;",
                "        progressed = true;",
                "      } else if (status != Status::kUnavailable) {",
                "        return status;",
                "      }",
                "      if (progressed) continue;",
            ]
        )
    public_lines.extend(
        [
            "      if (!progressed) break;",
            "    }",
            "    return executed == 0 ? Status::kUnavailable : Status::kOk;",
            "  }",
            "  xrobot::transport::can::CanFrameReceiver CanReceiver(",
            "      std::string_view link) noexcept {",
            "    return ports_.CanReceiver(link);",
            "  }",
            "  xrobot::runtime::Runtime& runtime() noexcept { return runtime_; }",
            "",
            " private:",
        ]
    )

    private_lines: list[str] = []
    for index, instance in enumerate(instances):
        implementation = instance.manifest.document["spec"]["implementation"]
        private_lines.append(
            f"  {implementation['class']} module_{index}_{{{_cpp_string(instance.name)}}};"
        )
    for index, (_, executor, full_name) in enumerate(executor_specs):
        private_lines.append(
            f"  xrobot::runtime::CooperativeExecutor<{executor['queue_depth']}> "
            f"executor_{index}_{{{_cpp_string(full_name)}, {executor['priority']}}};"
        )
    if periodic_specs:
        private_lines.append(
            f"  xrobot::runtime::StaticPeriodicScheduler<{len(periodic_specs)}> "
            'scheduler_{"periodic"};'
        )
    private_lines.append("  NodePorts ports_;")
    for instance_index, instance in enumerate(instances):
        if instance.config.get("ports"):
            mappings = ",\n".join(
                "      xrobot::runtime::NameMapping{"
                f"{_cpp_string(local)}, {_cpp_string(global_name)}}}"
                for local, global_name in sorted(instance.config["ports"].items())
            )
            private_lines.extend(
                [
                    f"  inline static constexpr std::array<xrobot::runtime::NameMapping, {len(instance.config['ports'])}> port_mappings_{instance_index}_{{{{",
                    mappings,
                    "  }};",
                    f"  xrobot::runtime::MappedPortResolver ports_{instance_index}_{{port_mappings_{instance_index}_}};",
                ]
            )
        if instance.config.get("hardware"):
            mappings = ",\n".join(
                "      xrobot::runtime::NameMapping{"
                f"{_cpp_string(local)}, {_cpp_string(global_name)}}}"
                for local, global_name in sorted(instance.config["hardware"].items())
            )
            private_lines.extend(
                [
                    f"  inline static constexpr std::array<xrobot::runtime::NameMapping, {len(instance.config['hardware'])}> hardware_mappings_{instance_index}_{{{{",
                    mappings,
                    "  }};",
                    f"  xrobot::runtime::MappedHardwareResolver hardware_{instance_index}_{{hardware_mappings_{instance_index}_}};",
                ]
            )
        for parameter_index, parameter in enumerate(instance.parameters):
            private_lines.append(
                _parameter_declaration(instance_index, parameter_index, parameter)
            )
        if instance.parameters:
            private_lines.append(
                f"  xrobot::runtime::StaticParameterRegistry<{len(instance.parameters)}> "
                f"parameters_{instance_index}_;"
            )
    for index, _ in enumerate(instances):
        private_lines.append(f"  xrobot::runtime::ModuleContext context_{index}_;")
    executor_slot_entries = ", ".join(
        f"xrobot::runtime::ExecutorSlot{{&executor_{index}_}}"
        for index, _ in enumerate(executor_specs)
    )
    module_slot_entries = ", ".join(
        f"xrobot::runtime::ModuleSlot{{&module_{index}_, &context_{index}_}}"
        for index, _ in enumerate(instances)
    )
    private_lines.extend(
        [
            f"  std::array<xrobot::runtime::ExecutorSlot, {len(executor_specs)}> executor_slots_{{{{{executor_slot_entries}}}}};",
            f"  std::array<xrobot::runtime::ModuleSlot, {len(instances)}> module_slots_{{{{{module_slot_entries}}}}};",
            "  xrobot::runtime::Runtime runtime_;",
            "  bool configuration_attempted_{};",
            "  bool configured_{};",
            "  bool seal_generated_ports_{};",
            "};",
        ]
    )

    return f"""\
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

{include_lines}
#include "node_config.hpp"
#include "node_ports.hpp"
#include "xrobot/runtime/cooperative_executor.hpp"
#include "xrobot/runtime/mapped_resolver.hpp"
#include "xrobot/runtime/module_context.hpp"
#include "xrobot/runtime/parameter.hpp"
#include "xrobot/runtime/parameter_registry.hpp"
#include "xrobot/runtime/periodic_scheduler.hpp"
#include "xrobot/runtime/runtime.hpp"

// Generated by xrctl. Do not edit.
namespace {namespace} {{

{chr(10).join(public_lines)}
{chr(10).join(private_lines)}

}}  // namespace {namespace}
"""


def _executor_report(plan: DeploymentPlan) -> dict[str, Any]:
    nodes: dict[str, list[dict[str, Any]]] = {
        name: [] for name in plan.deployment["nodes"]
    }
    for instance in plan.instances:
        for executor in instance.manifest.document["spec"]["executors"]:
            nodes[instance.node].append(
                {
                    **executor,
                    "name": f"{instance.name}__{executor['name']}",
                    "instance": instance.name,
                }
            )
    for executors in nodes.values():
        executors.sort(key=lambda item: item["name"])
    return {"nodes": nodes}


def _memory_report(plan: DeploymentPlan) -> dict[str, Any]:
    nodes: dict[str, Any] = {}
    for node_name in sorted(plan.deployment["nodes"]):
        node_instances = [item for item in plan.instances if item.node == node_name]
        executors = [
            executor
            for instance in node_instances
            for executor in instance.manifest.document["spec"]["executors"]
        ]
        node_routes = [
            route
            for route in plan.routes
            if route.source_node == node_name or node_name in route.destination_nodes
        ]
        queue_slots = sum(int(item["queue_depth"]) for item in executors)
        parameters = [
            parameter
            for instance in node_instances
            for parameter in instance.parameters
        ]
        parameter_sizes = {
            "bool": 1,
            "int32": 4,
            "uint32": 4,
            "float32": 4,
            "float64": 8,
        }
        nodes[node_name] = {
            "module_instances": len(node_instances),
            "executor_count": len(executors),
            "executor_stack_bytes": sum(int(item["stack_bytes"]) for item in executors),
            "executor_queue_slots": queue_slots,
            "executor_queue_bytes_estimate": queue_slots * 8,
            "parameter_count": len(parameters),
            "parameter_value_bytes": sum(
                parameter_sizes[item.type_name] for item in parameters
            ),
            "route_count": len(node_routes),
            "route_buffer_bytes_minimum": sum(
                route.max_wire_payload_size for route in node_routes
            ),
        }
    return {"assumptions": {"mcu_pointer_bytes": 4, "work_item_bytes": 8}, "nodes": nodes}


def write_deployment(
    plan: DeploymentPlan, output: Path, authoritative_lock: Path | None = None
) -> None:
    output.mkdir(parents=True, exist_ok=True)
    lock_path = authoritative_lock or (output / "deployment.lock.yaml")
    existing = _load_existing_lock(lock_path, plan.name)
    node_ids = _allocate_ids(
        list(plan.deployment["nodes"]), existing.get("nodes", {}), 1, 255
    )
    route_ids = _allocate_ids(
        [route.name for route in plan.routes], existing.get("routes", {}), 8, 511
    )

    lock = {
        "api_version": "xrobot.io/v1alpha1",
        "kind": "DeploymentLock",
        "metadata": {"deployment": plan.name},
        "deployment_hash": plan.deployment_hash,
        "schema_hash": plan.schema_hash,
        "protocol_version": 1,
        "nodes": node_ids,
        "routes": route_ids,
        "types": dict(sorted(plan.type_hashes.items())),
        "backends": {"xrobot-can": "1"},
    }
    lock_content = _yaml(lock)
    _write_if_changed(lock_path, lock_content)
    if lock_path != output / "deployment.lock.yaml":
        _write_if_changed(output / "deployment.lock.yaml", lock_content)

    for directory in (output / "nodes", output / "reports"):
        if directory.exists():
            shutil.rmtree(directory)
    _write_if_changed(output / "nodes/can_link.hpp", _can_link_header())

    resolved = {
        "api_version": "xrobot.io/v1alpha1",
        "kind": "ResolvedDeployment",
        "metadata": {"name": plan.name},
        "deployment_hash": plan.deployment_hash,
        "schema_hash": plan.schema_hash,
        "nodes": {
            name: {
                "id": node_ids[name],
                **plan.deployment["nodes"][name],
            }
            for name in sorted(plan.deployment["nodes"])
        },
        "routes": [
            _route_document(route, route_ids[route.name]) for route in plan.routes
        ],
    }
    _write_if_changed(output / "deployment.resolved.yaml", _yaml(resolved))

    module_graph = {
        "instances": [
            {
                "name": item.name,
                "package": item.package,
                "module": item.module,
                "node": item.node,
                "ports": item.config.get("ports", {}),
                "parameters": item.config.get("parameters", {}),
            }
            for item in plan.instances
        ]
    }
    routes_report = {
        "routes": [
            _route_document(route, route_ids[route.name]) for route in plan.routes
        ]
    }
    budget_report = {
        "links": {
            budget.name: {
                "bitrate_bps": budget.bitrate_bps,
                "reserved_utilization": round(budget.reserved_utilization, 9),
                "route_utilization": round(budget.route_utilization, 9),
                "total_utilization": round(budget.total_utilization, 9),
                "utilization_limit": round(budget.utilization_limit, 9),
                "within_budget": budget.within_budget,
            }
            for budget in plan.link_budgets
        }
    }
    _write_if_changed(output / "reports/module_graph.json", _json(module_graph))
    _write_if_changed(output / "reports/routes.json", _json(routes_report))
    _write_if_changed(output / "reports/link_budget.yaml", _yaml(budget_report))
    _write_if_changed(output / "reports/executors.yaml", _yaml(_executor_report(plan)))
    _write_if_changed(output / "reports/memory.yaml", _yaml(_memory_report(plan)))
    composition_blockers = {
        node_name: _composition_blockers(plan, node_name)
        for node_name in sorted(plan.deployment["nodes"])
    }
    _write_if_changed(
        output / "reports/composition.yaml",
        _yaml(
            {
                "nodes": {
                    node_name: {
                        "ready": not blockers,
                        "blockers": blockers,
                    }
                    for node_name, blockers in composition_blockers.items()
                }
            }
        ),
    )

    for node_name, node_id in node_ids.items():
        node_dir = output / "nodes" / node_name
        _write_if_changed(
            node_dir / "node_config.hpp",
            _node_header(plan, node_name, node_id, route_ids),
        )
        _write_if_changed(
            node_dir / "node_descriptor.cpp", _node_descriptor(node_name)
        )
        _write_if_changed(
            node_dir / "node_ports.hpp",
            _node_ports(plan, node_name, route_ids),
        )
        if not composition_blockers[node_name]:
            _write_if_changed(
                node_dir / "node_composition.hpp",
                _node_composition(plan, node_name),
            )
