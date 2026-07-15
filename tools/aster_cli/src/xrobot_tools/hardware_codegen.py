"""Static libxr hardware composition for generated MCU nodes."""

from __future__ import annotations

import re
import struct
from typing import Any

from xrobot_tools.deployment import DeploymentError, DeploymentPlan, Instance


_RESOURCE_TYPES = {
    "can": "xrobot::backend::libxr::ClassicCanEndpoint",
    "uart": "xrobot::backend::libxr::UartResource",
    "spi": "xrobot::backend::libxr::SpiResource",
    "gpio": "xrobot::backend::libxr::GpioResource",
    "gpio_interrupt": "xrobot::backend::libxr::GpioResource",
    "pwm": "xrobot::backend::libxr::PwmResource",
    "usb_cdc": "xrobot::backend::libxr::ByteStreamEndpoint",
}
_DRIVER_RESOURCES = {
    "bmi088/spi": {
        "spi": "spi",
        "accel_cs": "gpio",
        "gyro_cs": "gpio",
        "data_ready": "gpio_interrupt",
        "heater": "pwm",
    },
    "motor/dji-group": {"can": "can"},
    "motor/dm-group": {"can": "can"},
    "supercap-ctrl/shu-can": {"can": "can"},
    "referee/ui-writer": {"uart": "uart"},
    "vision-link/srm-vcp": {"vcp": "usb_cdc"},
}
_DRIVER_TYPES = {
    "bmi088/spi": "srm.imu.Bmi088Sensor",
    "motor/dji-group": "srm.hardware.MotorGroup/v1",
    "motor/dm-group": "srm.hardware.MotorGroup/v1",
    "supercap-ctrl/shu-can": "srm.power.SuperCapLink",
    "referee/ui-writer": "srm.hardware.RefereeUiWriter/v1",
    "vision-link/srm-vcp": "srm.hardware.VisionTransport/v1",
}
_DRIVER_BUILD_REQUIREMENTS = {
    "bmi088/spi": ("bmi088", "srm::bmi088-libxr"),
    "motor/dji-group": ("motor", "srm::motor-libxr"),
    "motor/dm-group": ("motor", "srm::motor-libxr"),
    "supercap-ctrl/shu-can": ("supercap-ctrl", "srm::supercap-ctrl-libxr"),
    "referee/ui-writer": ("referee", "srm::referee-libxr"),
    "vision-link/srm-vcp": ("vision-link", "srm::vision-link-libxr"),
}
_CPP_IDENTIFIER = re.compile(r"[^A-Za-z0-9_]")


def _identifier(value: str) -> str:
    result = _CPP_IDENTIFIER.sub("_", value)
    if not result or result[0].isdigit():
        result = f"_{result}"
    return result


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


def hardware_blockers(plan: DeploymentPlan, node_name: str) -> list[str]:
    profile = plan.hardware[node_name]["spec"]
    resources = profile["resources"]
    devices = profile.get("devices", {})
    module_bindings, device_names, resource_names = _scope(plan, node_name)
    blockers: list[str] = []

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
        expected = _DRIVER_RESOURCES.get(driver)
        if expected is None:
            blockers.append(f"device {name}: driver {driver!r} is not supported")
            continue
        supplied = device["resources"]
        missing = sorted(set(expected) - set(supplied))
        extra = sorted(set(supplied) - set(expected))
        if missing:
            blockers.append(
                f"device {name}: missing resources {', '.join(missing)}"
            )
        if extra:
            blockers.append(
                f"device {name}: unknown resources {', '.join(extra)}"
            )
        for local_name, expected_kind in expected.items():
            resource_name = supplied.get(local_name)
            resource = resources.get(resource_name) if resource_name else None
            if resource is not None and resource["kind"] != expected_kind:
                blockers.append(
                    f"device {name}: resource {local_name} requires "
                    f"{expected_kind}, got {resource['kind']}"
                )

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
                provided_type = _DRIVER_TYPES.get(devices[binding]["driver"])
            elif resources.get(binding, {}).get("kind") == "uart":
                provided_type = "xrobot.hardware.ByteReader/v1"
            else:
                provided_type = None
            if provided_type is not None and provided_type != required_type:
                blockers.append(
                    f"instance {instance.name}: hardware {local_name} requires "
                    f"{required_type}, but {binding} provides {provided_type}"
                )

    identifiers: dict[str, str] = {}
    for name in sorted(resource_names | device_names | module_bindings):
        identifier = _identifier(name)
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
    requirements = {
        _DRIVER_BUILD_REQUIREMENTS[plan.hardware[node_name]["spec"]["devices"][name]["driver"]]
        for name in device_names
        if plan.hardware[node_name]["spec"]["devices"][name]["driver"]
        in _DRIVER_BUILD_REQUIREMENTS
    }
    return tuple(sorted(requirements))


def _require_int(
    value: Any, location: str, minimum: int, maximum: int
) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise DeploymentError(
            f"{location} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def _require_float(value: Any, location: str, minimum: float = 0.0) -> float:
    if type(value) not in (int, float):
        raise DeploymentError(f"{location} must be numeric")
    result = float(value)
    if not result >= minimum or result == float("inf"):
        raise DeploymentError(f"{location} must be finite and >= {minimum}")
    return result


def _indexed(
    parameters: dict[str, Any], name: str, index: int, count: int, default: Any
) -> Any:
    value = parameters.get(name, default)
    if isinstance(value, list):
        if len(value) != count:
            raise DeploymentError(
                f"hardware parameter {name!r} requires {count} values"
            )
        return value[index]
    return value


def _u(value: int, suffix: str = "U") -> str:
    return f"{value}{suffix}"


def _f(value: float) -> str:
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    return f"std::bit_cast<float>(std::uint32_t{{0x{bits:08x}U}})"


def _direction(value: Any, location: str) -> str:
    values = {
        "normal": "srm::motor::Direction::kNormal",
        "reverse": "srm::motor::Direction::kReverse",
    }
    if value not in values:
        raise DeploymentError(f"{location} must be 'normal' or 'reverse'")
    return values[value]


def _pid(parameters: dict[str, Any], name: str, location: str) -> str:
    value = parameters.get(name, {})
    if not isinstance(value, dict):
        raise DeploymentError(f"{location}.{name} must be an object")
    fields = []
    for field in ("kp", "ki", "kd", "integral_limit", "output_limit"):
        number = _require_float(value.get(field, 0.0), f"{location}.{name}.{field}")
        fields.append(f".{field} = {_f(number)}")
    return "srm::motor::MotorPidConfig{" + ", ".join(fields) + "}"


def _dji_configs(name: str, device: dict[str, Any]) -> tuple[str, int]:
    parameters = device.get("parameters", {})
    ids = parameters.get("ids")
    if not isinstance(ids, list) or not ids:
        raise DeploymentError(f"device {name}: ids must be a non-empty list")
    model_name = parameters.get("model", "m3508")
    models = {
        "m2006": "srm::motor::DjiMotorModel::kM2006",
        "m3508": "srm::motor::DjiMotorModel::kM3508",
        "gm6020": "srm::motor::DjiMotorModel::kGm6020",
    }
    if model_name not in models:
        raise DeploymentError(f"device {name}: unknown DJI model {model_name!r}")
    count = len(ids)
    entries: list[str] = []
    for index, raw_id in enumerate(ids):
        motor_id = _require_int(raw_id, f"device {name}.ids[{index}]", 1, 8)
        command_value = _indexed(
            parameters,
            "command_direction",
            index,
            count,
            _indexed(parameters, "direction", index, count, "normal"),
        )
        feedback_value = _indexed(
            parameters, "feedback_direction", index, count, "normal"
        )
        encoder_zero = _require_int(
            _indexed(parameters, "encoder_zero", index, count, 0),
            f"device {name}.encoder_zero[{index}]",
            0,
            8191,
        )
        reduction_ratio = _require_float(
            _indexed(parameters, "reduction_ratio", index, count, 1.0),
            f"device {name}.reduction_ratio[{index}]",
            0.000001,
        )
        timeout_ms = _require_int(
            parameters.get("feedback_timeout_ms", 50),
            f"device {name}.feedback_timeout_ms",
            1,
            60_000,
        )
        entries.append(
            "      srm::motor::DjiMotorConfig{"
            f".model = {models[model_name]}, .id = {_u(motor_id)}, "
            f".command_direction = {_direction(command_value, f'device {name}.command_direction')}, "
            f".feedback_direction = {_direction(feedback_value, f'device {name}.feedback_direction')}, "
            f".encoder_zero = {_u(encoder_zero)}, "
            f".reduction_ratio = {_f(reduction_ratio)}, "
            f".position_pid = {_pid(parameters, 'position_pid', f'device {name}')}, "
            f".velocity_pid = {_pid(parameters, 'velocity_pid', f'device {name}')}, "
            f".feedback_timeout_ns = {_u(timeout_ms * 1_000_000, 'ULL')}"
            "},"
        )
    declaration = (
        f"  inline static constexpr std::array<srm::motor::DjiMotorConfig, {count}> "
        f"config_{_identifier(name)}_{{{{\n"
        + "\n".join(entries)
        + "\n  }};"
    )
    return declaration, count


def _dm_configs(name: str, device: dict[str, Any]) -> tuple[str, int]:
    parameters = device.get("parameters", {})
    tx_ids = parameters.get("tx_ids")
    rx_ids = parameters.get("rx_ids")
    if not isinstance(tx_ids, list) or not tx_ids:
        raise DeploymentError(f"device {name}: tx_ids must be a non-empty list")
    if not isinstance(rx_ids, list) or len(rx_ids) != len(tx_ids):
        raise DeploymentError(f"device {name}: rx_ids must match tx_ids")
    models = {
        "j4310": "srm::motor::DmMotorModel::kJ4310",
        "h6215": "srm::motor::DmMotorModel::kH6215",
        "j8009p": "srm::motor::DmMotorModel::kJ8009P",
    }
    model_name = parameters.get("model", "j8009p")
    if model_name not in models:
        raise DeploymentError(f"device {name}: unknown DM model {model_name!r}")
    count = len(tx_ids)
    entries: list[str] = []
    for index, raw_tx_id in enumerate(tx_ids):
        tx_id = _require_int(raw_tx_id, f"device {name}.tx_ids[{index}]", 0, 2047)
        rx_id = _require_int(rx_ids[index], f"device {name}.rx_ids[{index}]", 0, 2047)
        command_value = _indexed(
            parameters,
            "command_direction",
            index,
            count,
            _indexed(parameters, "direction", index, count, "normal"),
        )
        feedback_value = _indexed(
            parameters, "feedback_direction", index, count, "normal"
        )
        timeout_ms = _require_int(
            parameters.get("feedback_timeout_ms", 50),
            f"device {name}.feedback_timeout_ms",
            1,
            60_000,
        )
        entries.append(
            "      srm::motor::DmMotorConfig{"
            f".model = {models[model_name]}, .tx_id = {_u(tx_id)}, "
            f".rx_id = {_u(rx_id)}, "
            f".command_direction = {_direction(command_value, f'device {name}.command_direction')}, "
            f".feedback_direction = {_direction(feedback_value, f'device {name}.feedback_direction')}, "
            f".feedback_timeout_ns = {_u(timeout_ms * 1_000_000, 'ULL')}"
            "},"
        )
    declaration = (
        f"  inline static constexpr std::array<srm::motor::DmMotorConfig, {count}> "
        f"config_{_identifier(name)}_{{{{\n"
        + "\n".join(entries)
        + "\n  }};"
    )
    return declaration, count


def _bmi_config(
    name: str,
    device: dict[str, Any],
    resources: dict[str, dict[str, Any]],
) -> str:
    parameters = device.get("parameters", {})
    accel_ranges = {
        "3g": "srm::imu::Bmi088AccelerometerRange::k3g",
        "6g": "srm::imu::Bmi088AccelerometerRange::k6g",
        "12g": "srm::imu::Bmi088AccelerometerRange::k12g",
        "24g": "srm::imu::Bmi088AccelerometerRange::k24g",
    }
    gyro_ranges = {
        2000: "srm::imu::Bmi088GyroscopeRange::k2000DegreesPerSecond",
        1000: "srm::imu::Bmi088GyroscopeRange::k1000DegreesPerSecond",
        500: "srm::imu::Bmi088GyroscopeRange::k500DegreesPerSecond",
        250: "srm::imu::Bmi088GyroscopeRange::k250DegreesPerSecond",
        125: "srm::imu::Bmi088GyroscopeRange::k125DegreesPerSecond",
    }
    accel_range = parameters.get("accelerometer_range", "6g")
    gyro_range = parameters.get("gyroscope_range_dps", 2000)
    if accel_range not in accel_ranges:
        raise DeploymentError(f"device {name}: unsupported accelerometer range")
    if gyro_range not in gyro_ranges:
        raise DeploymentError(f"device {name}: unsupported gyroscope range")
    if parameters.get("accelerometer_rate_hz", 800) != 800:
        raise DeploymentError(f"device {name}: only 800 Hz accelerometer is supported")
    if parameters.get("gyroscope_rate_hz", 2000) != 2000:
        raise DeploymentError(f"device {name}: only 2000 Hz gyroscope is supported")
    spi = resources[device["resources"]["spi"]]
    heater = resources[device["resources"]["heater"]]
    spi_frequency = _require_int(
        spi.get("options", {}).get("frequency_hz", 10_000_000),
        f"device {name}.spi_frequency_hz",
        1,
        50_000_000,
    )
    heater_frequency = _require_int(
        heater.get("options", {}).get("frequency_hz", 20_000),
        f"device {name}.heater_frequency_hz",
        1,
        1_000_000,
    )
    return (
        "srm::imu::LibxrBmi088Config{"
        f".spi_frequency_hz = {_u(spi_frequency)}, "
        f".heater_frequency_hz = {_u(heater_frequency)}, "
        ".transfer_timeout_ms = 5U, .driver = {"
        f".accelerometer_range = {accel_ranges[accel_range]}, "
        f".gyroscope_range = {gyro_ranges[gyro_range]}, "
        ".accelerometer_configuration = 0xabU, "
        ".gyroscope_bandwidth = 0x81U, "
        ".reset_delay_ns = 80000000ULL}}"
    )


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
    dji_devices = sorted(
        name for name in device_names if devices[name]["driver"] == "motor/dji-group"
    )
    dm_devices = sorted(
        name for name in device_names if devices[name]["driver"] == "motor/dm-group"
    )
    dji_bus_resources = sorted(
        {devices[name]["resources"]["can"] for name in dji_devices}
    )

    includes = {
        "xrobot/backend/libxr/resources.hpp",
        "xrobot/backend/libxr/uart_reader_adapter.hpp",
        "xrobot/runtime/hardware_registry.hpp",
    }
    for name in device_names:
        driver = devices[name]["driver"]
        if driver == "bmi088/spi":
            includes.add("srm/imu/bmi088/libxr_sensor.hpp")
        elif driver == "motor/dji-group":
            includes.add("srm/motor/dji_motor_group.hpp")
        elif driver == "motor/dm-group":
            includes.add("srm/motor/dm_motor_group.hpp")
        elif driver == "supercap-ctrl/shu-can":
            includes.add("srm/power/supercap/libxr_shu_can_link.hpp")
        elif driver == "referee/ui-writer":
            includes.add("srm/referee/libxr_ui_writer.hpp")
        elif driver == "vision-link/srm-vcp":
            includes.add("srm/vision/byte_stream_transport.hpp")
    include_lines = "\n".join(f'#include "{item}"' for item in sorted(includes))

    declarations: list[str] = []
    for name in dji_devices:
        declaration, _ = _dji_configs(name, devices[name])
        declarations.append(declaration)
    for name in dm_devices:
        declaration, _ = _dm_configs(name, devices[name])
        declarations.append(declaration)

    fields: list[str] = []
    for name in sorted(resource_names):
        cpp_type = _RESOURCE_TYPES[resources[name]["kind"]]
        fields.append(f"  {cpp_type}* resource_{_identifier(name)}_{{}};")
    for name in direct_resources:
        fields.append(
            "  std::optional<xrobot::backend::libxr::UartReaderAdapter> "
            f"direct_{_identifier(name)}_;"
        )
    for name in dji_bus_resources:
        fields.append(
            f"  std::optional<srm::motor::DjiCommandBus> dji_bus_{_identifier(name)}_;"
        )
    for name in sorted(device_names):
        driver = devices[name]["driver"]
        identifier = _identifier(name)
        if driver == "bmi088/spi":
            cpp_type = "srm::imu::LibxrBmi088Sensor"
        elif driver == "motor/dji-group":
            cpp_type = "srm::motor::DjiMotorGroup"
        elif driver == "motor/dm-group":
            cpp_type = "srm::motor::DmMotorGroup"
        elif driver == "supercap-ctrl/shu-can":
            capacity = _require_int(
                devices[name].get("parameters", {}).get("receive_queue_depth", 8),
                f"device {name}.receive_queue_depth",
                1,
                64,
            )
            cpp_type = f"srm::power::LibxrShuSuperCapLink<{capacity}U>"
        elif driver == "referee/ui-writer":
            cpp_type = "srm::referee::LibxrRefereeUiWriter"
        else:
            cpp_type = "srm::vision::ByteStreamVisionTransport"
        fields.append(f"  std::optional<{cpp_type}> device_{identifier}_;")

    initialize: list[str] = []
    for name in sorted(resource_names):
        resource = resources[name]
        initialize.extend(
            [
                "    if (const auto status = Resolve(",
                f"            physical, \"{resource['resource']}\", resource_{_identifier(name)}_);",
                "        !IsOk(status)) return status;",
            ]
        )
    for name in direct_resources:
        initialize.append(
            f"    direct_{_identifier(name)}_.emplace(*resource_{_identifier(name)}_);"
        )
    for name in dji_bus_resources:
        initialize.append(
            f"    dji_bus_{_identifier(name)}_.emplace(*resource_{_identifier(name)}_);"
        )

    initialized_devices: list[str] = []
    for name in sorted(device_names):
        device = devices[name]
        driver = device["driver"]
        identifier = _identifier(name)
        bindings = {
            local: f"*resource_{_identifier(global_name)}_"
            for local, global_name in device["resources"].items()
        }
        if driver == "bmi088/spi":
            config = _bmi_config(name, device, resources)
            initialize.append(
                f"    device_{identifier}_.emplace({bindings['spi']}, "
                f"{bindings['accel_cs']}, {bindings['gyro_cs']}, "
                f"{bindings['data_ready']}, {bindings['heater']}, clock, {config});"
            )
            initialized_devices.append(name)
        elif driver == "motor/dji-group":
            bus = _identifier(device["resources"]["can"])
            initialize.append(
                f"    device_{identifier}_.emplace(*dji_bus_{bus}_, clock, "
                f"config_{identifier}_);"
            )
            initialized_devices.append(name)
        elif driver == "motor/dm-group":
            initialize.append(
                f"    device_{identifier}_.emplace({bindings['can']}, clock, "
                f"config_{identifier}_);"
            )
            initialized_devices.append(name)
        elif driver == "supercap-ctrl/shu-can":
            parameters = device.get("parameters", {})
            tx_id = _require_int(parameters.get("tx_id", 0x210), f"device {name}.tx_id", 0, 2047)
            rx_id = _require_int(parameters.get("rx_id", 0x211), f"device {name}.rx_id", 0, 2047)
            initialize.append(
                f"    device_{identifier}_.emplace({bindings['can']}, "
                "srm::power::LibxrShuSuperCapConfig{"
                f".transmit_id = {_u(tx_id)}, .receive_id = {_u(rx_id)}}});"
            )
            initialized_devices.append(name)
        elif driver == "referee/ui-writer":
            initialize.append(
                f"    device_{identifier}_.emplace({bindings['uart']});"
            )
        elif driver == "vision-link/srm-vcp":
            initialize.append(
                f"    device_{identifier}_.emplace({bindings['vcp']});"
            )
    for name in initialized_devices:
        method = (
            "InitializeHardware"
            if devices[name]["driver"] == "bmi088/spi"
            else "Initialize"
        )
        initialize.extend(
            [
                f"    if (const auto status = device_{_identifier(name)}_->{method}();",
                "        !IsOk(status)) return status;",
            ]
        )

    for binding in sorted(module_bindings):
        object_name = (
            f"device_{_identifier(binding)}_"
            if binding in devices
            else f"direct_{_identifier(binding)}_"
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

    start: list[str] = []
    if not dm_devices:
        start.append("    (void)execution;")
    for name in dm_devices:
        start.extend(
            [
                f"    if (const auto status = device_{_identifier(name)}_->Start(execution);",
                "        !IsOk(status)) return status;",
            ]
        )
    start.extend(["    started_ = true;", "    return Status::kOk;"])

    exchange: list[str] = []
    for name in dji_devices + dm_devices:
        exchange.extend(
            [
                f"    if (const auto status = device_{_identifier(name)}_->Exchange(now_ns, execution);",
                "        !IsOk(status)) return status;",
                "    result = Status::kOk;",
            ]
        )
    for name in dji_bus_resources:
        exchange.extend(
            [
                f"    if (const auto status = dji_bus_{_identifier(name)}_->Flush(execution);",
                "        !IsOk(status)) return status;",
                "    result = Status::kOk;",
            ]
        )
    exchange.append("    return result;")

    can_accessors = [
        f"    if (name == \"{name}\") return resource_{_identifier(name)}_;"
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

// Generated by xrctl. Do not edit.
namespace xrobot::generated::{node_name} {{

class NodeHardware {{
 public:
  xrobot::runtime::Status Initialize(
      xrobot::runtime::HardwareResolver& physical,
      xrobot::runtime::SteadyClock& clock) noexcept {{
    using xrobot::runtime::IsOk;
    using xrobot::runtime::Status;
    if (initialized_) return Status::kInvalidState;
{chr(10).join(initialize)}
  }}

  xrobot::runtime::Status Start(
      const xrobot::runtime::ExecutionContext& execution) noexcept {{
    using xrobot::runtime::IsOk;
    using xrobot::runtime::Status;
    if (!initialized_ || started_) return Status::kInvalidState;
{chr(10).join(start)}
  }}

  xrobot::runtime::Status Exchange(
      std::uint64_t now_ns,
      const xrobot::runtime::ExecutionContext& execution) noexcept {{
    using xrobot::runtime::IsOk;
    using xrobot::runtime::Status;
    if (!started_) return Status::kInvalidState;
    auto result = Status::kUnavailable;
{chr(10).join(exchange)}
  }}

  xrobot::runtime::HardwareResolver& hardware() noexcept {{ return hardware_; }}

  xrobot::backend::libxr::ClassicCanEndpoint* CanEndpoint(
      std::string_view name) noexcept {{
{chr(10).join(can_accessors)}
    return nullptr;
  }}

 private:
  template <typename Device>
  static xrobot::runtime::Status Resolve(
      xrobot::runtime::HardwareResolver& physical, std::string_view name,
      Device*& output) noexcept {{
    void* raw{{}};
    const auto status = physical.Resolve(name, Device::TypeName(), raw);
    output = status == xrobot::runtime::Status::kOk
                 ? static_cast<Device*>(raw) : nullptr;
    return status;
  }}

{chr(10).join(declarations)}
{chr(10).join(fields)}
  xrobot::runtime::StaticHardwareRegistry<{capacity}U> hardware_;
  bool initialized_{{}};
  bool started_{{}};
}};

}}  // namespace xrobot::generated::{node_name}
"""
