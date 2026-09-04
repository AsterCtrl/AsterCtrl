"""Bounded, deterministic Linux and Zephyr deployment emitters."""

from __future__ import annotations

import json
import re
import tempfile
from pathlib import Path
from typing import Any

from .deployment import write_deployment_bundle
from .graph import load_modules, resolve_deployment
from .models import load_application, load_deployment, load_package, load_workspace
from .protobuf import generate_from_proto
from .validation import dump_yaml, validate_mapping


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(path)


def _cpp_string(value: str) -> str:
    return json.dumps(value)


def _identifier(value: str, index: int) -> str:
    return f"instance_{re.sub(r'[^A-Za-z0-9_]', '_', value)}_{index}"


def _bounded_value(value: float | int | None) -> tuple[str, str]:
    if value is None:
        return "0.0", "false"
    return f"{float(value):.9g}", "true"


def _resolve(base: Path, value: str) -> Path:
    path = Path(value)
    return (base / path).resolve() if not path.is_absolute() else path.resolve()


def _generate_bounded_types(workspace: Any, output_root: Path) -> tuple[Path, ...]:
    """Generate each Package protobuf contract into the shared types directory."""

    generated: dict[str, bytes] = {}
    contract_headers: dict[str, str] = {}
    with tempfile.TemporaryDirectory(prefix="aster-deployment-types-") as temporary:
        staging = Path(temporary)
        for entry in workspace.packages:
            package_root = _resolve(workspace.source.parent, entry.source)
            manifest = (
                package_root
                if package_root.name.endswith(".yaml")
                else package_root / "package.yaml"
            )
            package = load_package(manifest)
            proto_files = tuple(
                _resolve(manifest.parent, value) for value in package.exports.get("protos", ())
            )
            if not proto_files:
                continue
            bounds = (
                _resolve(manifest.parent, package.protobuf.bounds)
                if package.protobuf is not None and package.protobuf.bounds is not None
                else None
            )
            includes = (
                [_resolve(manifest.parent, value) for value in package.protobuf.includes]
                if package.protobuf is not None and package.protobuf.includes
                else sorted({path.parent for path in proto_files})
            )
            header_name = (
                f"{proto_files[0].stem}.pb.hpp"
                if len(proto_files) == 1
                else f"{re.sub(r'[^A-Za-z0-9_.-]', '_', package.metadata.name)}.pb.hpp"
            )
            candidate = staging / f"{len(generated)}-{header_name}"
            result = generate_from_proto(list(proto_files), candidate, bounds, includes)
            content = candidate.read_bytes()
            previous = generated.get(header_name)
            if previous is not None and previous != content:
                raise ValueError(
                    f"generated protobuf header collision for {header_name!r}; "
                    "rename one root .proto file"
                )
            if previous is not None:
                continue
            for type_name in result.declarations:
                owner = contract_headers.get(type_name)
                if owner is not None:
                    raise ValueError(
                        f"protobuf type {type_name!r} would be defined by both "
                        f"{owner!r} and {header_name!r}; consolidate the schemas into "
                        "one Package contract"
                    )
                contract_headers[type_name] = header_name
            generated[header_name] = content

    paths: list[Path] = []
    for name, content in sorted(generated.items()):
        path = output_root / "types" / name
        _write(path, content.decode("utf-8"))
        paths.append(path)
    return tuple(paths)


def _composition(
    node_name: str,
    node: dict[str, Any],
    host_name: str,
    instances: list[dict[str, Any]],
    routes: list[dict[str, Any]],
    budgets: dict[str, Any],
    deployment_id: str,
    artifact_input_digest: str,
) -> str:
    instance_rows = ",\n".join(
        "  {"
        f"{_cpp_string(str(item['name']))}, {_cpp_string(str(item['module']))}, "
        f"{_cpp_string(item['class_name'] or '')}, "
        f"{_cpp_string(str(item['config_json']))}"
        "}"
        for item in instances
    )
    route_rows = ",\n".join(
        "  {"
        f"{route['id']}U, {_cpp_string(route['schema_hash'])}, "
        f"{_cpp_string(route['schema_input_digest'])}, "
        f"{_cpp_string(route['schema_hash_source'])}, "
        f"{_cpp_string(route['from'])}, {_cpp_string(route['to'])}, "
        f"{_cpp_string(route['transport'])}, {route['max_size']}U"
        f", {route['max_encoded_size']}U"
        "}"
        for route in routes
    )
    budget_rows: list[str] = []
    for name, value in sorted(budgets.items()):
        limit, bounded = _bounded_value(value["limit"])
        budget_rows.append(
            f"  {{{_cpp_string(name)}, {float(value['used']):.9g}, {limit}, {bounded}}}"
        )
    executor_rows = ",\n".join(
        "  {"
        f"{_cpp_string(domain)}, {_cpp_string(executor['policy'])}, "
        f"{_cpp_string(executor['backend'])}, {executor['workers']}U, "
        f"{executor['priority']}, {executor['stack_bytes']}U, {executor['queue_depth']}U"
        "}"
        for domain, executor in sorted(node["executors"].items())
    )
    typed = bool(instances) and all(item["class_name"] and item["header"] for item in instances)
    if typed:
        class_pattern = re.compile(r"^(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*$")
        header_pattern = re.compile(r"^[A-Za-z0-9_./-]+$")
        for item in instances:
            if class_pattern.fullmatch(str(item["class_name"])) is None:
                raise ValueError(f"invalid C++ module class {item['class_name']!r}")
            if header_pattern.fullmatch(str(item["header"])) is None:
                raise ValueError(f"invalid C++ module header {item['header']!r}")
    include_condition = " && ".join(
        f"__has_include({_cpp_string(str(item['header']))})" for item in instances
    )
    includes = "\n".join(f"#include {_cpp_string(str(item['header']))}" for item in instances)
    typed_prefix = (
        f"#if {include_condition}\n"
        "#define ASTER_GENERATED_TYPED_COMPOSITION 1\n"
        f"{includes}\n"
        "#else\n"
        "#define ASTER_GENERATED_TYPED_COMPOSITION 0\n"
        "#endif\n"
        if typed
        else "#define ASTER_GENERATED_TYPED_COMPOSITION 0\n"
    )
    typed_members = "\n".join(
        f"  {item['class_name']} {_identifier(str(item['name']), index)}{{}};"
        for index, item in enumerate(instances)
    )
    config_members = "\n".join(
        f"  InstanceConfigurator config_{_identifier(str(item['name']), index)};"
        for index, item in enumerate(instances)
    )
    config_initializers = ",\n        ".join(
        f"config_{_identifier(str(item['name']), index)}"
        f"(core.configurator(), kInstances[{index}].config_json)"
        for index, item in enumerate(instances)
    )
    config_initializer_list = f"\n      : {config_initializers}" if config_initializers else ""
    configured_cores = "\n".join(
        f"    module_slots[{index}].core = WithInstanceConfigurator("
        f"core, config_{_identifier(str(item['name']), index)});"
        for index, item in enumerate(instances)
    )
    module_slots = ",\n".join(
        f"    {{&{_identifier(str(item['name']), index)}, {{}}, {_cpp_string(str(item['name']))}}}"
        for index, item in enumerate(instances)
    )
    return f"""// Generated by aster. Do not edit.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include "aster/module.hpp"
#include "aster/registry.hpp"

{typed_prefix}

namespace aster::generated {{
struct InstanceDescriptor {{
  std::string_view name;
  std::string_view module;
  std::string_view class_name;
  std::string_view config_json;
}};
struct NodeDescriptor {{
  std::uint32_t id;
  std::string_view name;
  std::string_view host;
}};
struct Route {{
  std::uint32_t id;
  std::string_view schema_hash;
  std::string_view schema_input_digest;
  std::string_view schema_hash_source;
  std::string_view source;
  std::string_view destination;
  std::string_view transport;
  std::size_t max_size;
  std::size_t max_encoded_size;
}};
struct ResourceBudget {{
  std::string_view name;
  double used;
  double limit;
  bool has_limit;
}};
struct ExecutorDescriptor {{
  std::string_view domain;
  std::string_view policy;
  std::string_view backend;
  std::size_t workers;
  int priority;
  std::size_t stack_bytes;
  std::size_t queue_depth;
}};

inline constexpr std::string_view kDeploymentId = {_cpp_string(deployment_id)};
inline constexpr std::string_view kArtifactInputDigest = {_cpp_string(artifact_input_digest)};
inline constexpr std::array<NodeDescriptor, 1> kNodes{{{{
  {{{node["id"]}U, {_cpp_string(node_name)}, {_cpp_string(host_name)}}}
}}}};
inline constexpr std::array<InstanceDescriptor, {len(instances)}> kInstances{{{{
{instance_rows}
}}}};
inline constexpr std::array<Route, {len(routes)}> kRoutes{{{{
{route_rows}
}}}};
inline constexpr std::array<ResourceBudget, {len(budget_rows)}> kResourceBudgets{{{{
{",\n".join(budget_rows)}
}}}};
inline constexpr std::array<ExecutorDescriptor, {len(node["executors"])}> kExecutors{{{{
{executor_rows}
}}}};
inline constexpr bool kTypedComposition =
    ASTER_GENERATED_TYPED_COMPOSITION != 0;
inline constexpr std::string_view kInstanceConfigKey = "config";

class InstanceConfigurator final : public ::aster::Configurator {{
 public:
  InstanceConfigurator(::aster::ConfiguratorRef fallback,
                       std::string_view config_json) noexcept
      : config_json_(config_json), fallback_(fallback) {{}}

  ::aster::Status Get(std::string_view key, std::span<std::byte> output,
                      std::size_t& written) const noexcept override {{
    if (key != kInstanceConfigKey) {{
      return fallback_.Get(key, output, written);
    }}
    written = config_json_.size();
    if (output.size() < written) {{
      return ::aster::Status::kCapacityExceeded;
    }}
    std::copy(config_json_.begin(), config_json_.end(),
              reinterpret_cast<char*>(output.data()));
    return ::aster::Status::kOk;
  }}

 private:
  std::string_view config_json_;
  ::aster::ConfiguratorRef fallback_;
}};

inline ::aster::CoreRef WithInstanceConfigurator(
    ::aster::CoreRef core, ::aster::Configurator& configurator) noexcept {{
  return ::aster::CoreRef(::aster::CoreHandles{{
      .configurator = ::aster::ConfiguratorRef(configurator),
      .logger = core.logger(),
      .executor = core.executor(),
      .channel = core.channel(),
      .rpc = core.rpc(),
      .parameter = core.parameter(),
      .clock = core.clock(),
      .allocator = core.allocator(),
      .hardware = core.hardware(),
  }});
}}

#if ASTER_GENERATED_TYPED_COMPOSITION
struct Composition {{
{typed_members}
{config_members}
  std::array<::aster::ModuleSlot, {len(instances)}> module_slots{{{{
{module_slots}
  }}}};
  std::array<::aster::RegistrySlot, 0> registry_slots{{}};

  explicit Composition(::aster::CoreRef core = {{}}) noexcept{config_initializer_list} {{
{configured_cores}
  }}

  Composition(const Composition&) = delete;
  Composition& operator=(const Composition&) = delete;
  Composition(Composition&&) = delete;
  Composition& operator=(Composition&&) = delete;

  [[nodiscard]] std::span<::aster::ModuleSlot> Modules() noexcept {{
    return module_slots;
  }}
  [[nodiscard]] std::span<::aster::RegistrySlot> Registries() noexcept {{
    return registry_slots;
  }}
}};
#else
struct Composition {{
  std::array<::aster::ModuleSlot, 0> module_slots{{}};
  std::array<::aster::RegistrySlot, 0> registry_slots{{}};

  explicit Composition(::aster::CoreRef = {{}}) noexcept {{}}

  [[nodiscard]] std::span<::aster::ModuleSlot> Modules() noexcept {{
    return module_slots;
  }}
  [[nodiscard]] std::span<::aster::RegistrySlot> Registries() noexcept {{
    return registry_slots;
  }}
}};
#endif
}}  // namespace aster::generated

#undef ASTER_GENERATED_TYPED_COMPOSITION
"""


def _node_routes(lock: dict[str, Any], node_name: str) -> list[dict[str, Any]]:
    return [
        route
        for route in lock["routes"]
        if route["source_node"] == node_name or route["destination_node"] == node_name
    ]


def _emit_linux(
    root: Path,
    composition: str,
    node_name: str,
    host_name: str,
    instances: list[dict[str, Any]],
    routes: list[dict[str, Any]],
    lock: dict[str, Any],
) -> None:
    instance_names = [str(instance["name"]) for instance in instances]
    transport_names = sorted(
        {route["transport"] for route in routes if route["transport"] != "local"}
    )
    _write(root / "composition.generated.hpp", composition)
    _write(
        root / "composition.generated.cpp",
        '#include "composition.generated.hpp"\n',
    )
    _write(
        root / "aster.generated.cmake",
        "# Generated by aster. Do not edit.\n"
        "add_library(aster_generated OBJECT ${CMAKE_CURRENT_LIST_DIR}/composition.generated.cpp)\n"
        "target_include_directories(aster_generated PUBLIC\n"
        "  ${CMAKE_CURRENT_LIST_DIR}\n"
        "  ${CMAKE_CURRENT_LIST_DIR}/../../types)\n"
        "if(ASTER_MODULE_INCLUDE_DIRS)\n"
        "  target_include_directories(aster_generated PRIVATE ${ASTER_MODULE_INCLUDE_DIRS})\n"
        "endif()\n"
        "target_link_libraries(aster_generated PUBLIC aster::core)\n"
        "target_compile_features(aster_generated PRIVATE cxx_std_20)\n",
    )
    _write(
        root / "aster.generated.yaml",
        dump_yaml(
            {
                "deployment_hash": lock["content_hash"],
                "deployment_id": lock["deployment_id"],
                "host": host_name,
                "instances": instances,
                "node": node_name,
                "node_id": lock["nodes"][node_name]["id"],
                "executors": lock["nodes"][node_name]["executors"],
                "hardware": lock["hardware"].get(host_name),
                "capability_bindings": {
                    name: lock["capability_bindings"][name]
                    for name in instance_names
                    if name in lock["capability_bindings"]
                },
                "transports": {name: lock["transports"][name] for name in transport_names},
                "resource_budgets": lock["resource_budgets"],
                "artifact": lock["artifacts"][node_name],
                "routes": routes,
            }
        ),
    )


def _emit_zephyr(
    root: Path,
    composition: str,
    node_name: str,
    host_name: str,
    board: str,
    routes: list[dict[str, Any]],
    stack_bytes: int,
    lock_hash: str,
    hardware: dict[str, Any],
    transports: dict[str, Any],
) -> None:
    _write(root / "composition.generated.hpp", composition)
    _write(
        root / "composition.generated.cpp",
        '#include "composition.generated.hpp"\n',
    )
    route_depth = max(1, len(routes))
    config = [
        "# Generated by aster. Do not edit.",
        "CONFIG_CPP=y",
        "CONFIG_STD_CPP20=y",
        "CONFIG_REQUIRES_FULL_LIBCPP=y",
        "CONFIG_ASTERCTRL=y",
        f"CONFIG_ASTERCTRL_ROUTE_COUNT={route_depth}",
        f"CONFIG_ASTERCTRL_STACK_BYTES={max(1024, stack_bytes)}",
    ]
    resources = hardware["resources"]
    kinds = {resource["kind"] for resource in resources.values()}
    if kinds.intersection({"can", "canfd"}):
        config.append("CONFIG_CAN=y")
    if "canfd" in kinds:
        config.append("CONFIG_CAN_FD_MODE=y")
    usb_transports = [item for item in transports.values() if item["type"] == "usb_cdc"]
    if "usb_cdc" in kinds:
        config.extend(
            (
                "CONFIG_SERIAL=y",
                "CONFIG_USB_DEVICE_STACK_NEXT=y",
                "CONFIG_USBD_CDC_ACM_CLASS=y",
                "CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT=y",
                "CONFIG_ASTERCTRL_USB_CDC_ACM=y",
            )
        )
        identifiers = {(item["options"]["vid"], item["options"]["pid"]) for item in usb_transports}
        if len(identifiers) == 1:
            vid, pid = identifiers.pop()
            config.extend(
                (
                    f"CONFIG_CDC_ACM_SERIAL_VID=0x{vid:04X}",
                    f"CONFIG_CDC_ACM_SERIAL_PID=0x{pid:04X}",
                    f"CONFIG_ASTERCTRL_USB_VID=0x{vid:04X}",
                    f"CONFIG_ASTERCTRL_USB_PID=0x{pid:04X}",
                )
            )
    _write(root / "aster.generated.conf", "\n".join(config) + "\n")
    route_comments = "\n".join(
        f"/* route {item['id']}: {item['from']} -> {item['to']} over {item['transport']} */"
        for item in routes
    )
    aliases = []
    devices: set[str] = set()
    for name, resource in sorted(resources.items()):
        alias = "aster-" + re.sub(r"[_.]", "-", name)
        aliases.append(f"    {alias} = &{resource['device']};")
        devices.add(resource["device"])
    alias_rows = "\n".join(aliases)
    enabled_devices = "\n\n".join(
        f'&{device} {{\n  status = "okay";\n}};' for device in sorted(devices)
    )
    _write(
        root / "aster.generated.overlay",
        "/* Generated by aster. Hardware ownership remains in the board DTS. */\n"
        f"/* board: {board}; deployment: {lock_hash} */\n"
        f"{route_comments}\n"
        "/ {\n"
        "  aliases {\n"
        f"{alias_rows}\n"
        "  };\n"
        "};\n\n"
        f"{enabled_devices}\n",
    )
    _write(
        root / "aster.generated.cmake",
        "# Include after find_package(Zephyr).\n"
        "target_include_directories(app PRIVATE\n"
        "  ${CMAKE_CURRENT_LIST_DIR}\n"
        "  ${CMAKE_CURRENT_LIST_DIR}/../../types)\n"
        "target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/composition.generated.cpp)\n",
    )


def emit_deployment(
    workspace_path: str | Path,
    deployment_path: str | Path,
    output: str | Path,
    *,
    release: bool = False,
) -> dict[str, Any]:
    output_root = Path(output).resolve()
    lock = resolve_deployment(workspace_path, deployment_path, release=release)
    validate_mapping(lock, "deployment-lock.schema.json", str(deployment_path))
    deployment = load_deployment(deployment_path)
    application = load_application(deployment.source.parent / deployment.application)
    workspace = load_workspace(workspace_path)
    modules = load_modules(workspace)
    application_instances = {item.name: item for item in application.instances}
    hosts = {item.name: item for item in deployment.hosts}
    _write(output_root / "deployment.lock.yaml", dump_yaml(lock))
    _generate_bounded_types(workspace, output_root)
    for node_name, node in sorted(lock["nodes"].items()):
        host_name = node["host"]
        host = hosts[host_name]
        root = output_root / "nodes" / node_name
        routes = _node_routes(lock, node_name)
        instance_specs = []
        for instance_name in node["instances"]:
            instance = application_instances[instance_name]
            module = modules[instance.module]
            instance_specs.append(
                {
                    "name": instance_name,
                    "module": instance.module,
                    "class_name": module.class_name,
                    "header": module.header,
                    "config": instance.config,
                    "config_json": json.dumps(
                        instance.config,
                        sort_keys=True,
                        separators=(",", ":"),
                        ensure_ascii=False,
                    ),
                }
            )
        budgets = {
            f"host.{name}": value
            for name, value in lock["resource_budgets"]["hosts"][host_name].items()
        }
        for transport_name in sorted(
            {route["transport"] for route in routes if route["transport"] != "local"}
        ):
            budgets[f"transport.{transport_name}.utilization"] = lock["resource_budgets"][
                "transports"
            ][transport_name]
        composition = _composition(
            node_name,
            node,
            host_name,
            instance_specs,
            routes,
            budgets,
            lock["deployment_id"],
            lock["artifacts"][node_name]["input_digest"],
        )
        if host.os == "zephyr":
            hardware = lock["hardware"].get(host_name)
            if hardware is None:
                raise ValueError(f"Zephyr host {host_name!r} has no resolved Hardware")
            transport_names = sorted(
                {route["transport"] for route in routes if route["transport"] != "local"}
            )
            _emit_zephyr(
                root,
                composition,
                node_name,
                host_name,
                host.board or "unknown",
                routes,
                lock["stack_bytes"][host_name],
                lock["content_hash"],
                hardware,
                {name: lock["transports"][name] for name in transport_names},
            )
        else:
            _emit_linux(
                root,
                composition,
                node_name,
                host_name,
                instance_specs,
                routes,
                lock,
            )
    write_deployment_bundle(output_root)
    return lock
