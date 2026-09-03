"""Static, Package-extensible hardware composition for generated MCU nodes."""

from __future__ import annotations

from typing import Iterable

from aster_tools.deployment import DeploymentError, DeploymentPlan, Instance
from aster_tools.hardware_plugin import (
    DeviceFragment,
    DeviceRenderContext,
    HardwareDriverProvider,
    HardwarePluginError,
    SharedFragment,
    cpp_identifier,
    load_hardware_providers,
)


_RESOURCE_TYPES = {
    "can": "aster::backend::libxr::ClassicCanEndpoint",
    "uart": "aster::backend::libxr::UartResource",
    "spi": "aster::backend::libxr::SpiResource",
    "gpio": "aster::backend::libxr::GpioResource",
    "gpio_interrupt": "aster::backend::libxr::GpioResource",
    "pwm": "aster::backend::libxr::PwmResource",
    "usb_cdc": "aster::backend::libxr::ByteStreamEndpoint",
}


def _node_instances(plan: DeploymentPlan, node_name: str) -> list[Instance]:
    return sorted(
        (item for item in plan.instances if item.node == node_name),
        key=lambda item: item.name,
    )


def _scope(
    plan: DeploymentPlan, node_name: str
) -> tuple[set[str], set[str], set[str]]:
    profile = plan.hardware[node_name]["spec"]
    resources = profile["resources"]
    devices = profile.get("devices", {})
    module_bindings = {
        binding
        for instance in _node_instances(plan, node_name)
        for binding in instance.config.get("hardware", {}).values()
    }
    link_resources = {
        endpoint["resource"]
        for link in plan.deployment["links"].values()
        for endpoint in link["endpoints"]
        if endpoint["node"] == node_name
    }
    device_names = {name for name in module_bindings if name in devices}
    resource_names = {
        name for name in module_bindings | link_resources if name in resources
    }
    for name in device_names:
        resource_names.update(devices[name]["resources"].values())
    return module_bindings, device_names, resource_names


def _providers(plan: DeploymentPlan) -> dict[str, HardwareDriverProvider]:
    try:
        return load_hardware_providers(plan.package_paths)
    except HardwarePluginError as error:
        raise DeploymentError(str(error)) from error


def hardware_blockers(plan: DeploymentPlan, node_name: str) -> list[str]:
    profile = plan.hardware[node_name]["spec"]
    resources = profile["resources"]
    devices = profile.get("devices", {})
    module_bindings, device_names, resource_names = _scope(plan, node_name)
    blockers: list[str] = []
    try:
        providers = _providers(plan)
    except DeploymentError as error:
        return [str(error)]

    for name in sorted(resource_names):
        resource = resources.get(name)
        if resource is None:
            blockers.append(f"resource {name}: not declared")
            continue
        if resource["backend"] != "libxr":
            blockers.append(
                f"resource {name}: backend {resource['backend']!r} is not supported"
            )
        if resource["kind"] not in _RESOURCE_TYPES:
            blockers.append(
                f"resource {name}: kind {resource['kind']!r} is not supported"
            )

    for name in sorted(device_names):
        device = devices[name]
        driver = device["driver"]
        provider = providers.get(driver)
        if provider is None:
            blockers.append(
                f"device {name}: driver {driver!r} has no Package provider"
            )
            continue
        supplied = device["resources"]
        missing = sorted(set(provider.resources) - set(supplied))
        extra = sorted(set(supplied) - set(provider.resources))
        if missing:
            blockers.append(f"device {name}: missing resources {', '.join(missing)}")
        if extra:
            blockers.append(f"device {name}: unknown resources {', '.join(extra)}")
        for local_name, expected_kind in provider.resources.items():
            resource_name = supplied.get(local_name)
            resource = resources.get(resource_name) if resource_name else None
            if resource is not None and resource["kind"] != expected_kind:
                blockers.append(
                    f"device {name}: resource {local_name} requires "
                    f"{expected_kind}, got {resource['kind']}"
                )
        if not missing and not extra:
            context = DeviceRenderContext(name, device, resources)
            try:
                blockers.extend(
                    f"device {name}: {message}" for message in provider.validate(context)
                )
            except HardwarePluginError as error:
                blockers.append(f"device {name}: {error}")

    for binding in sorted(module_bindings):
        if binding in devices:
            continue
        resource = resources.get(binding)
        if resource is None:
            blockers.append(f"module hardware {binding}: not declared")
        elif resource["kind"] != "uart":
            blockers.append(
                f"module hardware {binding}: direct {resource['kind']} "
                "resources are not supported"
            )

    for instance in _node_instances(plan, node_name):
        requirements = {
            item["name"]: item.get("type")
            for item in instance.manifest.document["spec"].get("hardware", [])
            if isinstance(item, dict)
        }
        for local_name, binding in instance.config.get("hardware", {}).items():
            required_type = requirements.get(local_name)
            if required_type is None:
                continue
            if binding in devices:
                provider = providers.get(devices[binding]["driver"])
                provided_type = provider.provided_type if provider is not None else None
            elif resources.get(binding, {}).get("kind") == "uart":
                provided_type = "aster.hardware.ByteReader/v1"
            else:
                provided_type = None
            if provided_type is not None and provided_type != required_type:
                blockers.append(
                    f"instance {instance.name}: hardware {local_name} requires "
                    f"{required_type}, but {binding} provides {provided_type}"
                )

    identifiers: dict[str, str] = {}
    for name in sorted(resource_names | device_names | module_bindings):
        identifier = cpp_identifier(name)
        previous = identifiers.get(identifier)
        if previous is not None and previous != name:
            blockers.append(
                f"hardware names {previous!r} and {name!r} have the same "
                "C++ identifier"
            )
        identifiers[identifier] = name
    return sorted(set(blockers))


def hardware_build_requirements(
    plan: DeploymentPlan, node_name: str
) -> tuple[tuple[str, str], ...]:
    _, device_names, _ = _scope(plan, node_name)
    devices = plan.hardware[node_name]["spec"].get("devices", {})
    providers = _providers(plan)
    requirements = {
        (provider.build_package, provider.build_target)
        for name in device_names
        if (provider := providers.get(devices[name]["driver"])) is not None
    }
    return tuple(sorted(requirements))


def _collect(values: Iterable[tuple[str, ...]]) -> list[str]:
    return [line for group in values for line in group]


def render_node_hardware(plan: DeploymentPlan, node_name: str) -> str:
    blockers = hardware_blockers(plan, node_name)
    if blockers:
        raise DeploymentError(
            f"node {node_name!r} hardware is not composable: " + "; ".join(blockers)
        )

    profile = plan.hardware[node_name]["spec"]
    resources = profile["resources"]
    devices = profile.get("devices", {})
    module_bindings, device_names, resource_names = _scope(plan, node_name)
    direct_resources = sorted(module_bindings & set(resources))
    can_resources = sorted(
        name for name in resource_names if resources[name]["kind"] == "can"
    )
    providers = _providers(plan)

    fragments: dict[str, DeviceFragment] = {}
    shared: dict[str, SharedFragment] = {}
    for name in sorted(device_names):
        provider = providers[devices[name]["driver"]]
        try:
            fragment = provider.render(DeviceRenderContext(name, devices[name], resources))
        except HardwarePluginError as error:
            raise DeploymentError(f"device {name}: {error}") from error
        fragments[name] = fragment
        for item in fragment.shared:
            previous = shared.get(item.key)
            if previous is not None and previous != item:
                raise DeploymentError(
                    f"hardware shared fragment {item.key!r} has conflicting definitions"
                )
            shared[item.key] = item

    includes = {
        "aster/backend/libxr/resources.hpp",
        "aster/backend/libxr/uart_reader_adapter.hpp",
        "aster/runtime/hardware_registry.hpp",
    }
    includes.update(_collect(item.includes for item in shared.values()))
    includes.update(_collect(item.includes for item in fragments.values()))
    include_lines = "\n".join(f'#include "{item}"' for item in sorted(includes))

    declarations = _collect(item.declarations for item in shared.values())
    declarations.extend(_collect(item.declarations for item in fragments.values()))
    fields: list[str] = []
    for name in sorted(resource_names):
        cpp_type = _RESOURCE_TYPES[resources[name]["kind"]]
        fields.append(f"  {cpp_type}* resource_{cpp_identifier(name)}_{{}};")
    for name in direct_resources:
        fields.append(
            "  std::optional<aster::backend::libxr::UartReaderAdapter> "
            f"direct_{cpp_identifier(name)}_;"
        )
    fields.extend(_collect(item.fields for item in shared.values()))
    fields.extend(_collect(item.fields for item in fragments.values()))

    initialize: list[str] = []
    for name in sorted(resource_names):
        resource = resources[name]
        initialize.extend(
            [
                "    if (const auto status = Resolve(",
                f"            physical, \"{resource['resource']}\", resource_{cpp_identifier(name)}_);",
                "        !IsOk(status)) return status;",
            ]
        )
    for name in direct_resources:
        initialize.append(
            f"    direct_{cpp_identifier(name)}_.emplace(*resource_{cpp_identifier(name)}_);"
        )
    initialize.extend(_collect(item.initialize for item in shared.values()))
    initialize.extend(_collect(item.initialize for item in fragments.values()))
    for binding in sorted(module_bindings):
        object_name = (
            f"device_{cpp_identifier(binding)}_"
            if binding in devices
            else f"direct_{cpp_identifier(binding)}_"
        )
        initialize.extend(
            [
                f"    if (const auto status = hardware_.Add(\"{binding}\", *{object_name});",
                "        !IsOk(status)) return status;",
            ]
        )
    initialize.extend(
        [
            "    if (const auto status = hardware_.Seal(); !IsOk(status)) return status;",
            "    initialized_ = true;",
            "    return Status::kOk;",
        ]
    )

    start = _collect(item.start for item in shared.values())
    start.extend(_collect(item.start for item in fragments.values()))
    if not start:
        start.append("    (void)execution;")
    start.extend(["    started_ = true;", "    return Status::kOk;"])

    exchange = _collect(item.exchange for item in fragments.values())
    exchange.extend(_collect(item.exchange for item in shared.values()))
    if not exchange:
        exchange.extend(["    (void)now_ns;", "    (void)execution;"])
    exchange.append("    return result;")

    can_accessors = [
        f"    if (name == \"{name}\") return resource_{cpp_identifier(name)}_;"
        for name in can_resources
    ]
    capacity = max(1, len(module_bindings))

    return f"""\
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

{include_lines}

// Generated by aster. Do not edit.
namespace aster::generated::{node_name} {{

class NodeHardware {{
 public:
  aster::runtime::Status Initialize(
      aster::runtime::HardwareResolver& physical,
      aster::runtime::SteadyClock& clock) noexcept {{
    using aster::runtime::IsOk;
    using aster::runtime::Status;
    if (initialized_) return Status::kInvalidState;
{chr(10).join(initialize)}
  }}

  aster::runtime::Status Start(
      const aster::runtime::ExecutionContext& execution) noexcept {{
    using aster::runtime::IsOk;
    using aster::runtime::Status;
    if (!initialized_ || started_) return Status::kInvalidState;
{chr(10).join(start)}
  }}

  aster::runtime::Status Exchange(
      std::uint64_t now_ns,
      const aster::runtime::ExecutionContext& execution) noexcept {{
    using aster::runtime::IsOk;
    using aster::runtime::Status;
    if (!started_) return Status::kInvalidState;
    auto result = Status::kUnavailable;
{chr(10).join(exchange)}
  }}

  aster::runtime::HardwareResolver& hardware() noexcept {{ return hardware_; }}

  aster::backend::libxr::ClassicCanEndpoint* CanEndpoint(
      std::string_view name) noexcept {{
{chr(10).join(can_accessors)}
    return nullptr;
  }}

 private:
  template <typename Device>
  static aster::runtime::Status Resolve(
      aster::runtime::HardwareResolver& physical, std::string_view name,
      Device*& output) noexcept {{
    void* raw{{}};
    const auto status = physical.Resolve(name, Device::TypeName(), raw);
    output = status == aster::runtime::Status::kOk
                 ? static_cast<Device*>(raw) : nullptr;
    return status;
  }}

{chr(10).join(declarations)}
{chr(10).join(fields)}
  aster::runtime::StaticHardwareRegistry<{capacity}U> hardware_;
  bool initialized_{{}};
  bool started_{{}};
}};

}}  // namespace aster::generated::{node_name}
"""
