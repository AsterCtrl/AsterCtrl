"""Public build-time extension API for hardware driver code generation."""

from __future__ import annotations

import importlib.util
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Any, Callable, Mapping

import yaml


class HardwarePluginError(ValueError):
    """Raised when a Package exports an invalid hardware provider."""


_CPP_IDENTIFIER = re.compile(r"[^A-Za-z0-9_]")


def cpp_identifier(value: str) -> str:
    result = _CPP_IDENTIFIER.sub("_", value)
    if not result or result[0].isdigit():
        result = f"_{result}"
    return result


def require_int(value: Any, location: str, minimum: int, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise HardwarePluginError(
            f"{location} must be an integer in [{minimum}, {maximum}]"
        )
    return value


def require_float(value: Any, location: str, minimum: float = 0.0) -> float:
    if type(value) not in (int, float):
        raise HardwarePluginError(f"{location} must be numeric")
    result = float(value)
    if not math.isfinite(result) or result < minimum:
        raise HardwarePluginError(f"{location} must be finite and >= {minimum}")
    return result


def float_literal(value: float) -> str:
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    return f"std::bit_cast<float>(std::uint32_t{{0x{bits:08x}U}})"


def unsigned_literal(value: int, suffix: str = "U") -> str:
    return f"{value}{suffix}"


@dataclass(frozen=True)
class SharedFragment:
    key: str
    includes: tuple[str, ...] = ()
    declarations: tuple[str, ...] = ()
    fields: tuple[str, ...] = ()
    initialize: tuple[str, ...] = ()
    start: tuple[str, ...] = ()
    exchange: tuple[str, ...] = ()


@dataclass(frozen=True)
class DeviceFragment:
    includes: tuple[str, ...]
    fields: tuple[str, ...]
    initialize: tuple[str, ...]
    declarations: tuple[str, ...] = ()
    start: tuple[str, ...] = ()
    exchange: tuple[str, ...] = ()
    shared: tuple[SharedFragment, ...] = ()


@dataclass(frozen=True)
class DeviceRenderContext:
    name: str
    device: Mapping[str, Any]
    resources: Mapping[str, Mapping[str, Any]]

    @property
    def identifier(self) -> str:
        return cpp_identifier(self.name)

    def resource_name(self, local_name: str) -> str:
        return self.device["resources"][local_name]

    def resource_identifier(self, local_name: str) -> str:
        return cpp_identifier(self.resource_name(local_name))

    def resource_expression(self, local_name: str) -> str:
        return f"*resource_{self.resource_identifier(local_name)}_"

    def resource(self, local_name: str) -> Mapping[str, Any]:
        return self.resources[self.resource_name(local_name)]


ValidateDriver = Callable[[DeviceRenderContext], tuple[str, ...]]
RenderDriver = Callable[[DeviceRenderContext], DeviceFragment]


@dataclass(frozen=True)
class HardwareDriverProvider:
    driver: str
    resources: Mapping[str, str]
    provided_type: str
    build_package: str
    build_target: str
    render: RenderDriver
    validate: ValidateDriver = lambda _context: ()


def _load_module(path: Path, package_name: str) -> ModuleType:
    module_name = f"_aster_hardware_{cpp_identifier(package_name)}"
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise HardwarePluginError(f"{path}: cannot load hardware provider module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_hardware_providers(
    package_paths: Mapping[str, Path],
) -> dict[str, HardwareDriverProvider]:
    providers: dict[str, HardwareDriverProvider] = {}
    for package_name, package_path in sorted(package_paths.items()):
        manifest_path = package_path / "package.yaml"
        if not manifest_path.is_file():
            continue
        document = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
        exports = document.get("spec", {}).get("exports", {})
        entries = exports.get("hardware_drivers", [])
        if not isinstance(entries, list):
            raise HardwarePluginError(
                f"{manifest_path}: exports.hardware_drivers must be an array"
            )
        modules: dict[Path, ModuleType] = {}
        for entry in entries:
            if not isinstance(entry, dict) or set(entry) != {
                "driver", "provider", "factory"
            }:
                raise HardwarePluginError(
                    f"{manifest_path}: each hardware driver export requires "
                    "driver, provider, and factory"
                )
            driver = entry["driver"]
            provider_path = (package_path / entry["provider"]).resolve()
            if package_path.resolve() not in provider_path.parents:
                raise HardwarePluginError(
                    f"{manifest_path}: provider path escapes the Package"
                )
            if not provider_path.is_file():
                raise HardwarePluginError(f"{provider_path}: provider not found")
            module = modules.get(provider_path)
            if module is None:
                module = _load_module(provider_path, package_name)
                modules[provider_path] = module
            factory = getattr(module, entry["factory"], None)
            if not callable(factory):
                raise HardwarePluginError(
                    f"{provider_path}: factory {entry['factory']!r} is not callable"
                )
            provider = factory()
            if not isinstance(provider, HardwareDriverProvider):
                raise HardwarePluginError(
                    f"{provider_path}: factory must return HardwareDriverProvider"
                )
            if provider.driver != driver:
                raise HardwarePluginError(
                    f"{provider_path}: provider driver {provider.driver!r} does not "
                    f"match export {driver!r}"
                )
            if provider.build_package != package_name:
                raise HardwarePluginError(
                    f"{provider_path}: build_package must be {package_name!r}"
                )
            if driver in providers:
                raise HardwarePluginError(
                    f"hardware driver {driver!r} is exported more than once"
                )
            providers[driver] = provider
    return providers
