"""Deterministic Application/Deployment graph compiler."""

from __future__ import annotations

import fnmatch
import hashlib
import re
from dataclasses import asdict
from pathlib import Path
from typing import Any

import jsonschema

from .models import (
    Deployment,
    Hardware,
    Module,
    Workspace,
    load_application,
    load_deployment,
    load_hardware,
    load_module,
    load_package,
    load_workspace,
)
from .protobuf import BoundedSchema, ProtobufProfileError, inspect_from_proto
from .validation import API_VERSION, canonical_json, validate_document


class GraphError(ValueError):
    """Raised when a valid document set cannot form a safe graph."""


def _reject_external_schema_references(schema: dict[str, Any], module_name: str) -> None:
    pending: list[Any] = [schema]
    while pending:
        value = pending.pop()
        if isinstance(value, dict):
            for key, child in value.items():
                if (
                    key in {"$ref", "$dynamicRef"}
                    and isinstance(child, str)
                    and not child.startswith("#")
                ):
                    raise GraphError(
                        f"module {module_name!r} parameters schema uses external "
                        f"reference {child!r}; only local fragment references are allowed"
                    )
                pending.append(child)
        elif isinstance(value, list):
            pending.extend(value)


def _validate_instance_config(instance: Any, module: Module) -> None:
    if module.parameters is None:
        if instance.config:
            raise GraphError(
                f"instance {instance.name!r} supplies config but module "
                f"{module.metadata.name!r} declares no parameters"
            )
        return
    try:
        jsonschema.Draft202012Validator.check_schema(module.parameters)
    except jsonschema.exceptions.SchemaError as error:
        raise GraphError(
            f"module {module.metadata.name!r} has an invalid parameters schema: {error.message}"
        ) from error
    _reject_external_schema_references(module.parameters, module.metadata.name)
    errors = sorted(
        jsonschema.Draft202012Validator(module.parameters).iter_errors(instance.config),
        key=lambda item: [str(part) for part in item.absolute_path],
    )
    if errors:
        error = errors[0]
        path = ".".join(str(part) for part in error.absolute_path)
        location = f" config.{path}" if path else " config"
        raise GraphError(f"instance {instance.name!r}{location}: {error.message}")


def _unique(items: list[str], location: str) -> None:
    duplicates = sorted({item for item in items if items.count(item) > 1})
    if duplicates:
        raise GraphError(f"{location} contains duplicate names: {', '.join(duplicates)}")


def _split_endpoint(value: str) -> tuple[str, str]:
    return tuple(value.split(".", 1))  # type: ignore[return-value]


def _resolve(base: Path, value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def _file_digest(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        raise GraphError(f"cannot digest input {path}: {error}") from error


_DT_NODE_LABEL = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_LINUX_INTERFACE = re.compile(r"^[A-Za-z0-9_.:-]{1,15}$")

# These are the public ranges in zephyr/Kconfig. Keep resolution ahead of the
# emitter so an oversized graph fails with a useful source-level error instead
# of being truncated or rejected later by Kconfig.
_ZEPHYR_LIMITS = {
    "module_capacity": 128,
    "channel_capacity": 512,
    "subscriber_capacity": 512,
    "rpc_capacity": 128,
    "route_capacity": 65_535,
    "maximum_message_size": 65_535,
    "executor_queue_depth": 256,
    "can_tx_queue_depth": 256,
    "hardware_capacity": 256,
    "executor_stack_bytes": 1_048_576,
}
_ZEPHYR_ARENA_BYTES = 4096
_ZEPHYR_MIN_PRIORITY = -128
_ZEPHYR_MAX_PRIORITY = 126
_CAN_FIRST_APPLICATION_ROUTE_ID = 8
_CAN_MAXIMUM_ROUTE_ID = 511
_CAN_MAXIMUM_FRAGMENTS = 16
_CAN_FRAGMENT_PAYLOAD = 6
_CAN_CONSERVATIVE_FRAME_BITS = 160


def _zephyr_fixed_ram(capacities: dict[str, int]) -> int:
    """Return a deliberately conservative fixed-runtime storage bound.

    The values include padding and Zephyr kernel-object overhead. The linker
    size gate remains authoritative; this bound exists so graph budgets do not
    omit the generated runtime's dominant fixed allocations.
    """

    modules = capacities["module_capacity"]
    channels = capacities["channel_capacity"]
    rpc_services = capacities["rpc_capacity"]
    message_size = capacities["maximum_message_size"]
    return (
        capacities["arena_bytes"]
        + 512  # executor object, thread control block, semaphores, and message queue
        + capacities["executor_queue_depth"] * 32
        + capacities["can_tx_queue_depth"] * 32
        + capacities["transport_storage_bytes"]
        + modules * 32  # ModuleSlot storage
        + (modules + 3) * 8  # RegistrySlot storage
        + channels * (96 + capacities["subscriber_capacity"] * 16)  # topics and subscribers
        + rpc_services * 160  # service descriptors and callbacks
        + rpc_services * (2 * message_size + 96)  # pending request/response slots
        + capacities["hardware_capacity"] * 48
    )


def _validate_zephyr_capacities(node_name: str, capacities: dict[str, int]) -> None:
    for name, maximum in _ZEPHYR_LIMITS.items():
        value = capacities[name]
        if value > maximum:
            label = name.replace("_", " ")
            raise GraphError(
                f"Zephyr node {node_name!r} {label} {value} exceeds Kconfig maximum {maximum}"
            )


def _validate_hardware(host: Any, profile: Hardware) -> None:
    if profile.platform != host.os:
        raise GraphError(f"host {host.name!r} uses {profile.platform} Hardware on {host.os}")
    if host.os == "zephyr" and profile.board != host.board:
        raise GraphError(f"Zephyr host {host.name!r} board does not match Hardware")
    for resource in profile.resources:
        if host.os == "zephyr":
            if resource.backend != "devicetree":
                raise GraphError(f"Zephyr resource {resource.name!r} must use devicetree backend")
            if _DT_NODE_LABEL.fullmatch(resource.device) is None:
                raise GraphError(
                    f"Zephyr resource {resource.name!r} device must be a Devicetree node label"
                )
        elif resource.backend == "socketcan":
            if resource.kind not in {"can", "canfd"}:
                raise GraphError(
                    f"SocketCAN resource {resource.name!r} must have can or canfd kind"
                )
            if _LINUX_INTERFACE.fullmatch(resource.device) is None:
                raise GraphError(f"SocketCAN resource {resource.name!r} has invalid interface name")
        elif resource.backend == "tty":
            if resource.kind not in {"uart", "usb_cdc"}:
                raise GraphError(f"TTY resource {resource.name!r} must have uart or usb_cdc kind")
            if not resource.device.startswith("/dev/") or ".." in resource.device.split("/"):
                raise GraphError(
                    f"TTY resource {resource.name!r} must map to an absolute /dev path"
                )
        else:
            raise GraphError(
                f"Linux resource {resource.name!r} cannot use {resource.backend!r} backend"
            )


def _assign_ids(
    values: list[tuple[str, int | None]], location: str, first_automatic_id: int = 1
) -> dict[str, int]:
    explicit = [item_id for _, item_id in values if item_id is not None]
    _unique([str(item_id) for item_id in explicit], f"{location} IDs")
    used = set(explicit)
    result: dict[str, int] = {}
    candidate = first_automatic_id
    for name, item_id in sorted(values):
        if item_id is None:
            while candidate in used:
                candidate += 1
            item_id = candidate
            used.add(item_id)
            candidate += 1
        result[name] = item_id
    return result


def _startup_order(application: Any) -> list[str]:
    names = {item.name for item in application.instances}
    dependencies: dict[str, set[str]] = {}
    for instance in application.instances:
        unknown = sorted(set(instance.startup_after) - names)
        if unknown:
            raise GraphError(
                f"instance {instance.name!r} starts after unknown instances: {', '.join(unknown)}"
            )
        if instance.name in instance.startup_after:
            raise GraphError(f"instance {instance.name!r} cannot start after itself")
        dependencies[instance.name] = set(instance.startup_after)
    order: list[str] = []
    while dependencies:
        ready = sorted(name for name, after in dependencies.items() if not after)
        if not ready:
            cycle = ", ".join(sorted(dependencies))
            raise GraphError(f"startup dependency cycle contains: {cycle}")
        order.extend(ready)
        for name in ready:
            del dependencies[name]
        for after in dependencies.values():
            after.difference_update(ready)
    return order


def load_modules(workspace: Workspace) -> dict[str, Module]:
    modules: dict[str, Module] = {}
    for entry in workspace.packages:
        package_root = _resolve(workspace.source.parent, entry.source)
        manifest_path = (
            package_root if package_root.name.endswith(".yaml") else package_root / "package.yaml"
        )
        package = load_package(manifest_path)
        if package.metadata.name != entry.name:
            raise GraphError(
                f"workspace package {entry.name!r} resolves to Package {package.metadata.name!r}"
            )
        for exported in package.exports.get("modules", ()):
            module = load_module(_resolve(manifest_path.parent, exported))
            key = f"{entry.name}/{module.metadata.name}"
            if key in modules:
                raise GraphError(f"module {key!r} is exported more than once")
            modules[key] = module
    return modules


def _load_package_schemas(workspace: Workspace) -> dict[str, BoundedSchema]:
    schemas: dict[str, BoundedSchema] = {}
    for entry in workspace.packages:
        package_root = _resolve(workspace.source.parent, entry.source)
        manifest = (
            package_root if package_root.name.endswith(".yaml") else package_root / "package.yaml"
        )
        package = load_package(manifest)
        proto_files = tuple(
            _resolve(manifest.parent, path) for path in package.exports.get("protos", ())
        )
        if package.protobuf is not None and not proto_files:
            raise GraphError(f"package {entry.name!r} configures protobuf without exporting protos")
        if not proto_files:
            continue
        bounds = (
            _resolve(manifest.parent, package.protobuf.bounds)
            if package.protobuf is not None and package.protobuf.bounds is not None
            else None
        )
        includes = (
            [_resolve(manifest.parent, path) for path in package.protobuf.includes]
            if package.protobuf is not None and package.protobuf.includes
            else sorted({path.parent for path in proto_files})
        )
        try:
            schemas[entry.name] = inspect_from_proto(list(proto_files), bounds, includes)
        except ProtobufProfileError as error:
            raise GraphError(
                f"package {entry.name!r} has invalid bounded protobuf inputs: {error}"
            ) from error
    return schemas


def compile_application(
    workspace_path: str | Path,
    application_path: str | Path,
    *,
    release: bool = False,
) -> dict[str, Any]:
    workspace = load_workspace(workspace_path)
    application = load_application(application_path)
    startup_order = _startup_order(application)
    modules = load_modules(workspace)
    package_schemas = _load_package_schemas(workspace)
    module_by_instance: dict[str, Module] = {}
    module_ref_by_instance = {instance.name: instance.module for instance in application.instances}
    for instance in application.instances:
        if instance.module not in modules:
            raise GraphError(
                f"instance {instance.name!r} references unknown module {instance.module!r}"
            )
        module_by_instance[instance.name] = modules[instance.module]
        _validate_instance_config(instance, modules[instance.module])

    domains = {item.name: item for item in application.domains}
    _unique([item.name for item in application.domains], "application domains")
    endpoint_ports: dict[str, Any] = {}
    endpoint_providers: dict[str, Any] = {}
    endpoint_requirements: dict[str, Any] = {}
    for instance_name, module in sorted(module_by_instance.items()):
        _unique([item.name for item in module.ports], f"module {module.metadata.name} ports")
        _unique(
            [item.name for item in module.providers], f"module {module.metadata.name} providers"
        )
        _unique(
            [item.name for item in module.requirements],
            f"module {module.metadata.name} requirements",
        )
        _unique(
            [item.name for item in module.capabilities],
            f"module {module.metadata.name} capabilities",
        )
        _unique([item.name for item in module.tasks], f"module {module.metadata.name} tasks")
        for port in module.ports:
            endpoint_ports[f"{instance_name}.{port.name}"] = port
        for provider in module.providers:
            if provider.domain and provider.domain not in domains:
                raise GraphError(
                    f"provider {instance_name}.{provider.name} uses undeclared "
                    f"domain {provider.domain!r}"
                )
            endpoint_providers[f"{instance_name}.{provider.name}"] = provider
        for requirement in module.requirements:
            if requirement.domain and requirement.domain not in domains:
                raise GraphError(
                    f"requirement {instance_name}.{requirement.name} uses undeclared "
                    f"domain {requirement.domain!r}"
                )
            endpoint_requirements[f"{instance_name}.{requirement.name}"] = requirement
        for task in module.tasks:
            if task.domain not in domains:
                raise GraphError(
                    f"task {instance_name}.{task.name} uses undeclared domain {task.domain!r}"
                )
            if (
                task.deadline_us is not None
                and task.period_us is not None
                and task.deadline_us > task.period_us
            ):
                raise GraphError(f"task {instance_name}.{task.name} deadline exceeds period")

    ordered_connections = sorted(
        application.connections, key=lambda item: (item.source, item.destination)
    )
    _unique(
        [f"{item.source}->{item.destination}" for item in ordered_connections],
        "application connections",
    )
    connection_ids = _assign_ids(
        [(f"{item.source}->{item.destination}", item.route_id) for item in ordered_connections],
        "application route",
        _CAN_FIRST_APPLICATION_ROUTE_ID,
    )
    connection_rows: list[dict[str, Any]] = []
    connected: set[str] = set()
    compatible = {("publisher", "subscriber"), ("rpc_client", "rpc_server")}
    for connection in ordered_connections:
        source = endpoint_ports.get(connection.source)
        destination = endpoint_ports.get(connection.destination)
        if source is None:
            raise GraphError(f"connection source {connection.source!r} does not exist")
        if destination is None:
            raise GraphError(f"connection destination {connection.destination!r} does not exist")
        if (source.kind, destination.kind) not in compatible:
            raise GraphError(
                f"connection {connection.source} -> {connection.destination} has "
                f"incompatible directions {source.kind} -> {destination.kind}"
            )
        if source.type_name != destination.type_name:
            raise GraphError(
                f"connection {connection.source} -> {connection.destination} type "
                f"mismatch: {source.type_name} != {destination.type_name}"
            )
        source_instance, _ = _split_endpoint(connection.source)
        destination_instance, _ = _split_endpoint(connection.destination)
        source_package = module_ref_by_instance[source_instance].split("/", 1)[0]
        destination_package = module_ref_by_instance[destination_instance].split("/", 1)[0]
        source_schema = package_schemas.get(source_package)
        destination_schema = package_schemas.get(destination_package)
        if source_schema is None or destination_schema is None:
            if release:
                missing_packages = sorted(
                    name
                    for name, schema in (
                        (source_package, source_schema),
                        (destination_package, destination_schema),
                    )
                    if schema is None
                )
                raise GraphError(
                    f"release graph requires exported bounded protobuf for packages: "
                    f"{', '.join(missing_packages)}"
                )
            if source.schema_hash is not None or destination.schema_hash is not None:
                raise GraphError(
                    f"connection {connection.source} -> {connection.destination} cannot "
                    "verify schema_hash without exported bounded protobuf at both endpoints"
                )
            schema_hash = hashlib.sha256(
                canonical_json({"development_type": source.type_name})
            ).hexdigest()
            schema_input_digest = schema_hash
            schema_hash_source = "development_fallback"
            max_encoded_size = connection.max_size or 1
        else:
            is_rpc = source.kind == "rpc_client"
            source_max_wire_size = source_schema.max_wire_size(source.type_name, rpc=is_rpc)
            destination_max_wire_size = destination_schema.max_wire_size(
                destination.type_name, rpc=is_rpc
            )
            contract_kind = "RPC method" if is_rpc else "message"
            if source_max_wire_size is None:
                raise GraphError(
                    f"port {connection.source!r} {contract_kind} {source.type_name!r} "
                    "is not exported "
                    f"by package {source_package!r} protobuf descriptors"
                )
            if destination_max_wire_size is None:
                raise GraphError(
                    f"port {connection.destination!r} {contract_kind} "
                    f"{destination.type_name!r} is not exported by package "
                    f"{destination_package!r} protobuf descriptors"
                )
            if source_schema.schema_hash != destination_schema.schema_hash:
                raise GraphError(
                    f"connection {connection.source} -> {connection.destination} has "
                    "mismatched descriptor-and-bounds schema hashes"
                )
            if source_max_wire_size != destination_max_wire_size:
                raise GraphError(
                    f"connection {connection.source} -> {connection.destination} has "
                    "mismatched maximum encoded sizes"
                )
            schema_hash = source_schema.schema_hash
            schema_input_digest = schema_hash
            schema_hash_source = "descriptor_bounds"
            max_encoded_size = source_max_wire_size
            for endpoint, port in (
                (connection.source, source),
                (connection.destination, destination),
            ):
                if port.schema_hash is not None and port.schema_hash != schema_hash:
                    raise GraphError(
                        f"port {endpoint!r} schema_hash assertion does not match the "
                        "descriptor-and-bounds hash"
                    )
        if connection.max_size is not None and connection.max_size < max_encoded_size:
            raise GraphError(
                f"connection {connection.source} -> {connection.destination} max_size "
                f"{connection.max_size} is below encoded maximum {max_encoded_size}"
            )
        max_size = connection.max_size or max_encoded_size
        for endpoint, port in (
            (connection.source, source),
            (connection.destination, destination),
        ):
            if port.max_rate_hz is not None and connection.max_rate_hz > port.max_rate_hz:
                raise GraphError(
                    f"connection rate {connection.max_rate_hz:g} Hz exceeds {endpoint} limit "
                    f"{port.max_rate_hz:g} Hz"
                )
        connected.update((connection.source, connection.destination))
        connection_rows.append(
            {
                "id": connection_ids[f"{connection.source}->{connection.destination}"],
                "from": connection.source,
                "to": connection.destination,
                "kind": "rpc" if source.kind.startswith("rpc_") else "channel",
                "type": source.type_name,
                "schema_hash": schema_hash,
                "schema_input_digest": schema_input_digest,
                "schema_hash_source": schema_hash_source,
                "qos": connection.qos,
                "max_rate_hz": connection.max_rate_hz,
                "max_size": max_size,
                "max_encoded_size": max_encoded_size,
            }
        )
    missing_ports = sorted(
        name for name, port in endpoint_ports.items() if port.required and name not in connected
    )
    if missing_ports:
        raise GraphError(f"required ports are not connected: {', '.join(missing_ports)}")

    bound_requirements: set[str] = set()
    binding_rows: list[dict[str, str]] = []
    for binding in sorted(application.bindings, key=lambda item: item.requirement):
        requirement = endpoint_requirements.get(binding.requirement)
        provider = endpoint_providers.get(binding.provider)
        if requirement is None:
            raise GraphError(f"binding requirement {binding.requirement!r} does not exist")
        if provider is None:
            raise GraphError(f"binding provider {binding.provider!r} does not exist")
        if requirement.interface != provider.interface:
            raise GraphError(
                f"binding {binding.requirement} -> {binding.provider} interface mismatch"
            )
        if requirement.domain and provider.domain and requirement.domain != provider.domain:
            raise GraphError(f"binding {binding.requirement} -> {binding.provider} domain mismatch")
        if binding.requirement in bound_requirements:
            raise GraphError(f"requirement {binding.requirement!r} is bound more than once")
        bound_requirements.add(binding.requirement)
        binding_rows.append({"requirement": binding.requirement, "provider": binding.provider})
    missing_requirements = sorted(
        name
        for name, requirement in endpoint_requirements.items()
        if not requirement.optional and name not in bound_requirements
    )
    if missing_requirements:
        raise GraphError(f"requirements are not bound: {', '.join(missing_requirements)}")

    instance_ports: dict[str, list[dict[str, Any]]] = {}
    for instance_name, module in sorted(module_by_instance.items()):
        package_name = module_ref_by_instance[instance_name].split("/", 1)[0]
        schema = package_schemas.get(package_name)
        rows: list[dict[str, Any]] = []
        for port in module.ports:
            is_rpc = port.kind.startswith("rpc_")
            maximum = schema.max_wire_size(port.type_name, rpc=is_rpc) if schema else None
            if maximum is None:
                if port.schema_hash is not None and schema is None:
                    raise GraphError(
                        f"port {instance_name}.{port.name!s} cannot verify schema_hash "
                        "without exported bounded protobuf"
                    )
                if schema is not None or release:
                    contract_kind = "RPC method" if is_rpc else "message"
                    raise GraphError(
                        f"port {instance_name}.{port.name!s} {contract_kind} "
                        f"{port.type_name!r} is not exported by package {package_name!r} "
                        "protobuf descriptors"
                    )
                # Development-only manifests without protobuf still need a
                # deterministic finite capacity. Release resolution rejects
                # this fallback above.
                maximum = 256
            if port.schema_hash is not None and schema is not None:
                if port.schema_hash != schema.schema_hash:
                    raise GraphError(
                        f"port {instance_name}.{port.name!s} schema_hash assertion does not "
                        "match the descriptor-and-bounds hash"
                    )
            rows.append(
                {
                    "name": port.name,
                    "kind": port.kind,
                    "type": port.type_name,
                    "max_encoded_size": maximum,
                }
            )
        instance_ports[instance_name] = rows

    graph = {
        "kind": "ApplicationGraph",
        "name": application.metadata.name,
        "instances": [
            {
                "name": name,
                "module": next(item.module for item in application.instances if item.name == name),
                "config": next(item.config for item in application.instances if item.name == name),
                "startup_after": list(
                    next(item.startup_after for item in application.instances if item.name == name)
                ),
                "resources": {
                    "static_ram_bytes": module_by_instance[name].static_ram_bytes,
                    "flash_bytes": module_by_instance[name].flash_bytes,
                },
                "ports": instance_ports[name],
                "capabilities": [asdict(item) for item in module_by_instance[name].capabilities],
            }
            for name in startup_order
        ],
        "connections": connection_rows,
        "bindings": binding_rows,
        "domains": [asdict(item) for item in application.domains],
        "startup_order": startup_order,
    }
    graph["content_hash"] = hashlib.sha256(canonical_json(graph)).hexdigest()
    return graph


def _choose_transport(
    deployment: Deployment,
    route_name: str,
    source_host: str,
    destination_host: str,
) -> tuple[str, str | None]:
    for rule in deployment.route_rules:
        if fnmatch.fnmatchcase(route_name, rule.match):
            transport = next(
                (item for item in deployment.transports if item.name == rule.transport), None
            )
            if transport is None:
                raise GraphError(f"route rule references unknown transport {rule.transport!r}")
            if not {source_host, destination_host}.issubset(transport.hosts):
                raise GraphError(
                    f"transport {transport.name!r} does not connect route {route_name}"
                )
            return transport.name, rule.qos
    candidates = sorted(
        item.name
        for item in deployment.transports
        if {source_host, destination_host}.issubset(item.hosts)
    )
    if not candidates:
        raise GraphError(f"no transport connects route {route_name}")
    return candidates[0], None


def resolve_deployment(
    workspace_path: str | Path,
    deployment_path: str | Path,
    *,
    release: bool = False,
) -> dict[str, Any]:
    workspace = load_workspace(workspace_path)
    deployment = load_deployment(deployment_path)
    application_path = _resolve(deployment.source.parent, deployment.application)
    application = load_application(application_path)
    app_graph = compile_application(workspace.source, application_path, release=release)
    modules = load_modules(workspace)
    hosts = {item.name: item for item in deployment.hosts}
    nodes = {item.name: item for item in deployment.nodes}
    node_ids = _assign_ids(
        [(item.name, item.node_id) for item in deployment.nodes],
        "deployment node",
    )
    _unique(list(hosts), "deployment hosts")
    _unique(list(nodes), "deployment nodes")
    for node in deployment.nodes:
        if node.host not in hosts:
            raise GraphError(f"node {node.name!r} references unknown host {node.host!r}")
    placements: dict[str, str] = {}
    for node in deployment.nodes:
        for instance in node.instances:
            if instance not in {item.name for item in application.instances}:
                raise GraphError(f"node {node.name!r} places unknown instance {instance!r}")
            if instance in placements:
                raise GraphError(f"instance {instance!r} is placed more than once")
            placements[instance] = node.name
    missing = sorted({item.name for item in application.instances} - set(placements))
    if missing:
        raise GraphError(f"instances are not placed: {', '.join(missing)}")

    app_domains = {item.name: item.time for item in application.domains}
    for node in deployment.nodes:
        unknown_domains = sorted(set(node.domains) - set(app_domains))
        if unknown_domains:
            raise GraphError(
                f"node {node.name!r} references unknown domains: {', '.join(unknown_domains)}"
            )
    unknown_host_budgets = sorted(set(deployment.host_budgets) - set(hosts))
    if unknown_host_budgets:
        raise GraphError(f"budgets reference unknown hosts: {', '.join(unknown_host_budgets)}")

    hardware: dict[str, Hardware] = {}
    for host in deployment.hosts:
        if host.os == "zephyr" and (host.hardware is None or host.board is None):
            raise GraphError(f"Zephyr host {host.name!r} requires board and hardware")
        if host.hardware is None:
            continue
        profile = load_hardware(_resolve(deployment.source.parent, host.hardware))
        _validate_hardware(host, profile)
        hardware[host.name] = profile
    package_transports: dict[str, set[str]] = {}
    for entry in workspace.packages:
        package_root = _resolve(workspace.source.parent, entry.source)
        manifest = (
            package_root if package_root.name.endswith(".yaml") else package_root / "package.yaml"
        )
        package = load_package(manifest)
        package_transports[entry.name] = set(package.exports.get("transports", ()))
    for transport in deployment.transports:
        if transport.type == "canfd":
            raise GraphError(
                f"transport {transport.name!r} requests canfd, but v0.2 implements "
                "only the classic CAN wire protocol"
            )
        if transport.type not in {"can", "canfd", "usb_cdc"}:
            if not transport.backend or not transport.package:
                raise GraphError(
                    f"transport {transport.name!r} type {transport.type!r} requires "
                    "a declarative backend and package"
                )
            exported = package_transports.get(transport.package)
            if exported is None:
                raise GraphError(
                    f"transport {transport.name!r} references unknown package {transport.package!r}"
                )
            if transport.backend not in exported:
                raise GraphError(
                    f"transport backend {transport.backend!r} is not exported by "
                    f"package {transport.package!r}"
                )
        if transport.type in {"can", "canfd"} and transport.bitrate_bps is None:
            raise GraphError(f"transport {transport.name!r} requires bitrate_bps")
        if transport.type == "can":
            bounded_options = {
                "poll_interval_us": (100, 1_000_000, 1_000),
                "retry_timeout_us": (100, 1_000_000, 5_000),
                "maximum_retries": (1, 15, 2),
                "reassembly_timeout_us": (100, 10_000_000, 100_000),
            }
            for option, (minimum, maximum, default) in bounded_options.items():
                value = transport.options.get(option, default)
                if (
                    isinstance(value, bool)
                    or not isinstance(value, int)
                    or not minimum <= value <= maximum
                ):
                    raise GraphError(
                        f"transport {transport.name!r} requires options.{option} "
                        f"in {minimum}..{maximum}"
                    )
        if transport.type == "usb_cdc":
            for option in ("vid", "pid"):
                value = transport.options.get(option)
                if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 65535:
                    raise GraphError(
                        f"transport {transport.name!r} requires options.{option} in 1..65535"
                    )
            poll_interval_us = transport.options.get("poll_interval_us", 1000)
            if (
                isinstance(poll_interval_us, bool)
                or not isinstance(poll_interval_us, int)
                or not 100 <= poll_interval_us <= 1_000_000
            ):
                raise GraphError(
                    f"transport {transport.name!r} requires options.poll_interval_us "
                    "in 100..1000000"
                )
            baud_rate = transport.options.get("baud_rate", 115200)
            if isinstance(baud_rate, bool) or baud_rate not in {
                9600,
                57600,
                115200,
                230400,
                460800,
                921600,
            }:
                raise GraphError(
                    f"transport {transport.name!r} requires a supported options.baud_rate"
                )
        maximum_mtu = {"can": 8, "canfd": 64}.get(transport.type)
        if maximum_mtu is not None and transport.mtu > maximum_mtu:
            raise GraphError(
                f"transport {transport.name!r} MTU {transport.mtu} exceeds "
                f"{transport.type} limit {maximum_mtu}"
            )
        if transport.type == "can" and transport.mtu != 8:
            raise GraphError(
                f"transport {transport.name!r} classic CAN MTU must be 8, got {transport.mtu}"
            )
        if transport.type == "shm" and len(set(transport.hosts)) > 1:
            raise GraphError(f"shared-memory transport {transport.name!r} cannot cross hosts")
        official = transport.type in {"can", "canfd", "usb_cdc"}
        if official and transport.resource is None:
            raise GraphError(f"transport {transport.name!r} requires a hardware resource")
        for host_name in transport.hosts:
            if host_name not in hosts:
                raise GraphError(
                    f"transport {transport.name!r} references unknown host {host_name!r}"
                )
            if transport.resource is not None:
                profile = hardware.get(host_name)
                if profile is None:
                    raise GraphError(
                        f"transport {transport.name!r} requires a Hardware profile on {host_name}"
                    )
                resources = {item.name: item for item in profile.resources}
                resource = resources.get(transport.resource)
                if resource is None:
                    raise GraphError(
                        f"transport {transport.name!r} uses missing resource "
                        f"{transport.resource!r} on {host_name}"
                    )
                accepted = {"can": {"can"}, "canfd": {"canfd", "can"}, "usb_cdc": {"usb_cdc"}}
                if transport.type in accepted and resource.kind not in accepted[transport.type]:
                    raise GraphError(
                        f"transport {transport.name!r} resource kind "
                        f"{resource.kind!r} is incompatible"
                    )
                expected_backend = (
                    "devicetree"
                    if hosts[host_name].os == "zephyr"
                    else "socketcan"
                    if transport.type in {"can", "canfd"}
                    else "tty"
                )
                if official and resource.backend != expected_backend:
                    raise GraphError(
                        f"transport {transport.name!r} resource on {host_name} must use "
                        f"{expected_backend} backend"
                    )

    transport_names = {item.name for item in deployment.transports}
    unknown_transport_budgets = sorted(set(deployment.transport_budgets) - transport_names)
    if unknown_transport_budgets:
        raise GraphError(
            f"budgets reference unknown transports: {', '.join(unknown_transport_budgets)}"
        )
    for rule in deployment.route_rules:
        if rule.transport not in transport_names:
            raise GraphError(f"route rule references unknown transport {rule.transport!r}")

    if deployment.time_authority and deployment.time_authority not in nodes:
        raise GraphError(f"time authority {deployment.time_authority!r} is not a node")
    time_domains = {item.name: item for item in deployment.time_domains}
    unknown_time_domains = sorted(set(time_domains) - set(app_domains))
    if unknown_time_domains:
        raise GraphError(
            f"deployment time mapping references unknown domains: {', '.join(unknown_time_domains)}"
        )
    for name, time_kind in app_domains.items():
        configured = time_domains.get(name)
        if configured is None:
            raise GraphError(f"application domain {name!r} has no deployment time mapping")
        if time_kind == "simulated" and configured.source != "simulated":
            raise GraphError(f"simulated domain {name!r} must use simulated time")
        if configured.source == "synced" and not (
            configured.authority or deployment.time_authority
        ):
            raise GraphError(f"synced domain {name!r} requires an authority")
        if configured.authority and configured.authority not in nodes:
            raise GraphError(
                f"time domain {name!r} authority {configured.authority!r} is not a node"
            )
    for node in deployment.nodes:
        enabled_domains = set(node.domains) or set(app_domains)
        time_sources = {
            "simulated" if time_domains[name].source == "simulated" else "production"
            for name in enabled_domains
        }
        if len(time_sources) > 1:
            raise GraphError(f"node {node.name!r} mixes simulated and production time domains")

    module_by_instance = {item.name: modules[item.module] for item in application.instances}
    for instance_name, module in module_by_instance.items():
        node = nodes[placements[instance_name]]
        host = hosts[node.host]
        if module.platforms and host.os not in module.platforms:
            raise GraphError(
                f"module {module.metadata.name!r} does not support {host.os} "
                f"for instance {instance_name!r}"
            )
        if node.domains:
            missing_domains = sorted({task.domain for task in module.tasks} - set(node.domains))
            if missing_domains:
                raise GraphError(
                    f"node {node.name!r} does not enable task domains for {instance_name}: "
                    f"{', '.join(missing_domains)}"
                )
    capability_bindings: dict[str, dict[str, Any]] = {}
    for instance_name, module in sorted(module_by_instance.items()):
        node = nodes[placements[instance_name]]
        host = hosts[node.host]
        profile = hardware.get(host.name)
        resolved: dict[str, Any] = {}
        for capability in module.capabilities:
            candidates = (
                sorted(
                    (item for item in profile.resources if item.kind == capability.kind),
                    key=lambda item: item.name,
                )
                if profile is not None
                else []
            )
            if not candidates:
                if capability.optional:
                    continue
                raise GraphError(
                    f"instance {instance_name!r} requires missing hardware capability "
                    f"{capability.name!r} ({capability.kind}) on host {host.name!r}"
                )
            if len(candidates) > 1:
                names = ", ".join(item.name for item in candidates)
                raise GraphError(
                    f"instance {instance_name!r} hardware capability {capability.name!r} "
                    f"({capability.kind}) has multiple providers on host {host.name!r}: {names}"
                )
            resource = candidates[0]
            resolved[capability.name] = {
                "kind": capability.kind,
                "resource": resource.name,
                "backend": resource.backend,
                "device": resource.device,
                "options": dict(sorted(resource.options.items())),
            }
        capability_bindings[instance_name] = resolved
    for node in deployment.nodes:
        if hosts[node.host].os != "zephyr":
            continue
        aliases: dict[str, tuple[str, str]] = {}
        for instance_name in node.instances:
            for name, binding in capability_bindings[instance_name].items():
                target = (binding["kind"], binding["device"])
                previous = aliases.get(name)
                if previous is not None and previous != target:
                    raise GraphError(
                        f"Zephyr node {node.name!r} hardware capability name {name!r} "
                        "maps to different devices; HardwareManager names are node-global"
                    )
                aliases[name] = target
    for binding in application.bindings:
        requirement_instance, _ = _split_endpoint(binding.requirement)
        provider_instance, _ = _split_endpoint(binding.provider)
        if placements[requirement_instance] != placements[provider_instance]:
            raise GraphError(
                f"provider binding {binding.requirement} -> {binding.provider} crosses nodes"
            )
    for instance in application.instances:
        for dependency in instance.startup_after:
            if placements[instance.name] != placements[dependency]:
                raise GraphError(
                    f"startup dependency {instance.name} -> {dependency} crosses nodes"
                )

    executor_lock: dict[str, dict[str, Any]] = {}
    for node in deployment.nodes:
        host = hosts[node.host]
        enabled_domains = set(node.domains) or set(app_domains)
        configured = {item.domain: item for item in node.executors}
        unknown_executor_domains = sorted(set(configured) - enabled_domains)
        if unknown_executor_domains:
            raise GraphError(
                f"node {node.name!r} configures executors for disabled domains: "
                f"{', '.join(unknown_executor_domains)}"
            )
        tasks_by_domain = {
            domain: [
                task
                for instance_name in node.instances
                for task in module_by_instance[instance_name].tasks
                if task.domain == domain
            ]
            for domain in sorted(enabled_domains)
        }
        if host.os == "zephyr" and len(tasks_by_domain) > 1:
            raise GraphError(
                f"Zephyr node {node.name!r} enables multiple executor domains; "
                "v0.2 supports exactly one executor domain per Zephyr node"
            )
        resolved: dict[str, Any] = {}
        for domain, tasks in tasks_by_domain.items():
            policy = configured.get(domain)
            policy_name = policy.policy if policy else "serial"
            workers = policy.workers if policy and policy.workers is not None else 1
            if policy_name == "worker_pool":
                if host.os != "linux":
                    raise GraphError(f"node {node.name!r} cannot use worker_pool on {host.os}")
                if policy is None or policy.workers is None:
                    workers = 2
            elif workers != 1:
                raise GraphError(
                    f"node {node.name!r} serial executor {domain!r} must use one worker"
                )
            required_stack = max((task.stack_bytes for task in tasks), default=1024)
            required_queue = max(1, sum(task.queue_depth for task in tasks))
            stack_bytes = (
                policy.stack_bytes
                if policy is not None and policy.stack_bytes is not None
                else required_stack
            )
            queue_depth = (
                policy.queue_depth
                if policy is not None and policy.queue_depth is not None
                else required_queue
            )
            if stack_bytes < required_stack:
                raise GraphError(
                    f"node {node.name!r} executor {domain!r} stack {stack_bytes} is below "
                    f"task requirement {required_stack}"
                )
            if queue_depth < required_queue:
                raise GraphError(
                    f"node {node.name!r} executor {domain!r} queue {queue_depth} is below "
                    f"task requirement {required_queue}"
                )
            priority = (
                policy.priority
                if policy is not None and policy.priority is not None
                else max((task.priority for task in tasks), default=0)
            )
            if host.os == "zephyr" and not (
                _ZEPHYR_MIN_PRIORITY <= priority <= _ZEPHYR_MAX_PRIORITY
            ):
                raise GraphError(
                    f"Zephyr node {node.name!r} executor {domain!r} priority {priority} "
                    f"is outside {_ZEPHYR_MIN_PRIORITY}..{_ZEPHYR_MAX_PRIORITY}"
                )
            resolved[domain] = {
                "policy": policy_name,
                "backend": (
                    "zephyr_work_queue"
                    if host.os == "zephyr"
                    else "linux_worker_pool"
                    if policy_name == "worker_pool"
                    else "linux_thread"
                ),
                "workers": workers,
                "priority": priority,
                "stack_bytes": stack_bytes,
                "queue_depth": queue_depth,
            }
        executor_lock[node.name] = resolved

    transport_by_name = {item.name: item for item in deployment.transports}
    routes: list[dict[str, Any]] = []
    utilization: dict[str, float] = {name: 0.0 for name in transport_by_name}
    for connection in app_graph["connections"]:
        source_instance, _ = _split_endpoint(connection["from"])
        destination_instance, _ = _split_endpoint(connection["to"])
        source_node = placements[source_instance]
        destination_node = placements[destination_instance]
        source_host = nodes[source_node].host
        destination_host = nodes[destination_node].host
        route_name = f"{connection['from']}->{connection['to']}"
        transport_name = "local"
        qos = connection["qos"]
        # Local is strictly in-process. Two nodes on one Host are still
        # separate Runtime processes and therefore require an explicit Link.
        if source_node != destination_node:
            transport_name, qos_override = _choose_transport(
                deployment, route_name, source_host, destination_host
            )
            if qos_override is not None:
                qos = qos_override
            transport = transport_by_name[transport_name]
            if transport.mtu <= 0:
                raise GraphError(f"transport {transport.name!r} has invalid MTU")
            if transport.type == "can":
                if not (
                    _CAN_FIRST_APPLICATION_ROUTE_ID <= connection["id"] <= _CAN_MAXIMUM_ROUTE_ID
                ):
                    raise GraphError(
                        f"CAN route {route_name!r} ID {connection['id']} is outside "
                        f"{_CAN_FIRST_APPLICATION_ROUTE_ID}..{_CAN_MAXIMUM_ROUTE_ID}"
                    )
                protocol_overhead = 12 if connection["kind"] == "rpc" else 2
                payload_size = int(connection["max_size"]) + protocol_overhead
                frame_count = (
                    1
                    if connection["kind"] == "channel" and payload_size <= 7
                    else (payload_size + _CAN_FRAGMENT_PAYLOAD - 1) // _CAN_FRAGMENT_PAYLOAD
                )
                if frame_count > _CAN_MAXIMUM_FRAGMENTS:
                    maximum_size = (
                        _CAN_MAXIMUM_FRAGMENTS * _CAN_FRAGMENT_PAYLOAD - protocol_overhead
                    )
                    raise GraphError(
                        f"CAN route {route_name!r} {connection['kind']} capacity "
                        f"{connection['max_size']} exceeds maximum {maximum_size} bytes "
                        f"({_CAN_MAXIMUM_FRAGMENTS} fragments)"
                    )
            if transport.bitrate_bps:
                bits = (
                    frame_count * _CAN_CONSERVATIVE_FRAME_BITS
                    if transport.type == "can"
                    else (
                        connection["max_size"]
                        + (connection["max_size"] + transport.mtu - 1) // transport.mtu * 8
                    )
                    * 8
                )
                utilization[transport_name] += (
                    bits * connection["max_rate_hz"] / transport.bitrate_bps
                )
        routes.append(
            {
                "id": connection["id"],
                "from": connection["from"],
                "to": connection["to"],
                "kind": connection["kind"],
                "type": connection["type"],
                "schema_hash": connection["schema_hash"],
                "schema_input_digest": connection["schema_input_digest"],
                "schema_hash_source": connection["schema_hash_source"],
                "source_node": source_node,
                "destination_node": destination_node,
                "transport": transport_name,
                "qos": qos,
                "max_size": connection["max_size"],
                "max_encoded_size": connection["max_encoded_size"],
                "max_rate_hz": connection["max_rate_hz"],
            }
        )
    outgoing_rpc: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for route in routes:
        if route["kind"] != "rpc":
            continue
        outgoing_rpc.setdefault((route["source_node"], route["type"]), []).append(route)
        if route["transport"] != "local" and (transport_by_name[route["transport"]].type != "can"):
            raise GraphError(
                f"v0.2 built-in external RPC route {route['from']} -> {route['to']} requires CAN"
            )
    for (source_node, service), service_routes in sorted(outgoing_rpc.items()):
        if (
            any(route["transport"] != "local" for route in service_routes)
            and len(service_routes) > 1
        ):
            raise GraphError(
                f"remote RPC service {service!r} is ambiguous on node {source_node!r}; "
                "v0.2 requires one destination per remote service"
            )
    for name, used in utilization.items():
        limit = deployment.transport_budgets.get(name, 0.8)
        if used > limit:
            raise GraphError(
                f"transport {name!r} utilization {used:.3f} exceeds budget {limit:.3f}"
            )

    can_routes = [
        route
        for route in routes
        if route["transport"] != "local" and transport_by_name[route["transport"]].type == "can"
    ]
    if can_routes:
        time_authorities = {
            configured.authority or deployment.time_authority
            for configured in deployment.time_domains
            if configured.source == "synced"
        }
        time_authorities.discard(None)
        if len(time_authorities) != 1:
            raise GraphError(
                "CAN application routes require exactly one synchronized time authority"
            )
        time_authority = next(iter(time_authorities))
        participating_nodes: set[str] = set()
        for transport_name in sorted({str(route["transport"]) for route in can_routes}):
            transport_routes = [
                route for route in can_routes if route["transport"] == transport_name
            ]
            peers_by_node: dict[str, set[str]] = {}
            for route in transport_routes:
                source_node = str(route["source_node"])
                destination_node = str(route["destination_node"])
                peers_by_node.setdefault(source_node, set()).add(destination_node)
                peers_by_node.setdefault(destination_node, set()).add(source_node)
            multiple_peers = sorted(
                name for name, peers in peers_by_node.items() if len(peers) != 1
            )
            if multiple_peers:
                raise GraphError(
                    f"v0.2 CAN transport {transport_name!r} supports one peer per node: "
                    + ", ".join(multiple_peers)
                )
            if time_authority not in peers_by_node:
                raise GraphError(
                    f"CAN transport {transport_name!r} does not connect synchronized "
                    f"time authority {time_authority!r}"
                )
            participating_nodes.update(peers_by_node)
        invalid_node_ids = sorted(name for name in participating_nodes if node_ids[name] > 255)
        if invalid_node_ids:
            raise GraphError("CAN links require node IDs in 1..255: " + ", ".join(invalid_node_ids))

    application_instances = {item["name"]: item for item in app_graph["instances"]}
    zephyr_runtime_by_node: dict[str, dict[str, int]] = {}
    for node in deployment.nodes:
        if hosts[node.host].os != "zephyr":
            continue
        node_routes = [
            route
            for route in routes
            if node.name in {route["source_node"], route["destination_node"]}
        ]
        ports = [
            port
            for instance_name in node.instances
            for port in application_instances[instance_name]["ports"]
        ]
        channel_ports = sum(port["kind"] in {"publisher", "subscriber"} for port in ports)
        subscriber_ports = sum(port["kind"] == "subscriber" for port in ports)
        rpc_ports = sum(port["kind"] in {"rpc_client", "rpc_server"} for port in ports)
        external_transports = {
            route["transport"] for route in node_routes if route["transport"] != "local"
        }
        capability_names = {
            name for instance_name in node.instances for name in capability_bindings[instance_name]
        }
        message_sizes = [int(port["max_encoded_size"]) for port in ports] + [
            int(route["max_size"]) for route in node_routes
        ]
        can_frame_counts = []
        for route in node_routes:
            if route["transport"] == "local" or transport_by_name[route["transport"]].type != "can":
                continue
            protocol_overhead = 12 if route["kind"] == "rpc" else 2
            payload_size = int(route["max_size"]) + protocol_overhead
            can_frame_counts.append(
                1
                if route["kind"] == "channel" and payload_size <= 7
                else (payload_size + _CAN_FRAGMENT_PAYLOAD - 1) // _CAN_FRAGMENT_PAYLOAD
            )
        transport_storage_bytes = 0
        for transport_name in sorted(external_transports):
            transport_routes = [
                route for route in node_routes if route["transport"] == transport_name
            ]
            transport = transport_by_name[transport_name]
            if transport.type == "usb_cdc":
                maximum_payload = max(int(route["max_size"]) for route in transport_routes)
                # PacketCodec owns one raw and two COBS-sized buffers. The
                # remaining fixed allowance covers its lifecycle Module,
                # Router entries, Channel bridges, padding and statistics.
                transport_storage_bytes += 768 + 4 * maximum_payload + 256 * len(transport_routes)
            elif transport.type == "can":
                maximum_payload = max(int(route["max_encoded_size"]) for route in transport_routes)
                # Control-plane handshake/reassembly, route-local sender or
                # receiver state, fixed payload buffers, and Adapter state.
                transport_storage_bytes += 1536 + 8 * maximum_payload + 512 * len(transport_routes)
        capacities = {
            "module_capacity": max(1, len(node.instances) + len(external_transports)),
            # Every declared port may bind a distinct local name. Routes remain
            # a lower bound for generated transport infrastructure.
            "channel_capacity": max(1, channel_ports, len(node_routes)),
            "subscriber_capacity": max(1, subscriber_ports, len(node_routes)),
            "rpc_capacity": max(1, rpc_ports, len(node_routes)),
            "route_capacity": max(1, len(node_routes)),
            "maximum_message_size": max(message_sizes, default=256),
            "executor_queue_depth": max(
                (int(executor["queue_depth"]) for executor in executor_lock[node.name].values()),
                default=16,
            )
            + len(external_transports),
            # A full maximum-size message can enter an initially empty Adapter
            # even when the controller has no mailbox available yet.
            "can_tx_queue_depth": max(can_frame_counts, default=1),
            "transport_storage_bytes": transport_storage_bytes,
            "hardware_capacity": max(1, len(capability_names)),
            "executor_stack_bytes": max(
                [1024]
                + [int(executor["stack_bytes"]) for executor in executor_lock[node.name].values()]
            ),
            "arena_bytes": _ZEPHYR_ARENA_BYTES,
        }
        _validate_zephyr_capacities(node.name, capacities)
        capacities["fixed_ram_bytes"] = _zephyr_fixed_ram(capacities)
        zephyr_runtime_by_node[node.name] = capacities

    stack_by_host: dict[str, int] = {name: 0 for name in hosts}
    static_ram_by_host: dict[str, int] = {name: 0 for name in hosts}
    runtime_ram_by_host: dict[str, int] = {name: 0 for name in hosts}
    flash_by_host: dict[str, int] = {name: 0 for name in hosts}
    for node_name, executors in executor_lock.items():
        host = nodes[node_name].host
        stack_by_host[host] += sum(
            executor["stack_bytes"] * executor["workers"] for executor in executors.values()
        )
    for instance_name, module in module_by_instance.items():
        host = nodes[placements[instance_name]].host
        static_ram_by_host[host] += module.static_ram_bytes
        flash_by_host[host] += module.flash_bytes
    for node_name, runtime in zephyr_runtime_by_node.items():
        runtime_ram_by_host[nodes[node_name].host] += runtime["fixed_ram_bytes"]
    resource_hosts: dict[str, Any] = {}
    for host_name, stack in stack_by_host.items():
        host_budget = deployment.host_budgets.get(host_name, {})
        profile = hardware.get(host_name)
        stack_limit = host_budget.get("stack_bytes")
        ram_limits = [
            value
            for value in (
                host_budget.get("ram_bytes"),
                profile.ram_bytes if profile else None,
            )
            if value is not None
        ]
        flash_limits = [
            value
            for value in (
                host_budget.get("flash_bytes"),
                profile.flash_bytes if profile else None,
            )
            if value is not None
        ]
        ram_limit = min(ram_limits) if ram_limits else None
        flash_limit = min(flash_limits) if flash_limits else None
        ram = stack + static_ram_by_host[host_name] + runtime_ram_by_host[host_name]
        flash = flash_by_host[host_name]
        if stack_limit is not None and stack > stack_limit:
            raise GraphError(f"host {host_name!r} stack {stack} exceeds budget {stack_limit}")
        if ram_limit is not None and ram > ram_limit:
            raise GraphError(f"host {host_name!r} RAM {ram} exceeds budget {ram_limit}")
        if flash_limit is not None and flash > flash_limit:
            raise GraphError(f"host {host_name!r} flash {flash} exceeds budget {flash_limit}")
        resource_hosts[host_name] = {
            "stack_bytes": {"used": stack, "limit": stack_limit},
            "ram_bytes": {"used": ram, "limit": ram_limit},
            "flash_bytes": {"used": flash, "limit": flash_limit},
        }

    startup_position = {name: position for position, name in enumerate(app_graph["startup_order"])}
    nodes_lock = {
        node.name: {
            "id": node_ids[node.name],
            "host": node.host,
            "instances": sorted(node.instances, key=lambda name: startup_position[name]),
            "executors": executor_lock[node.name],
            **(
                {"runtime": zephyr_runtime_by_node[node.name]}
                if node.name in zephyr_runtime_by_node
                else {}
            ),
        }
        for node in sorted(deployment.nodes, key=lambda item: item.name)
    }
    deployment_hash = hashlib.sha256(
        canonical_json(
            {
                "deployment": validate_document(deployment.source),
                "workspace": validate_document(workspace.source),
            }
        )
    ).hexdigest()
    deployment_id = hashlib.sha256(
        canonical_json(
            {
                "name": deployment.metadata.name,
                "application_hash": app_graph["content_hash"],
                "deployment_hash": deployment_hash,
            }
        )
    ).hexdigest()
    resource_budgets = {
        "hosts": dict(sorted(resource_hosts.items())),
        "transports": {
            name: {
                "used": round(utilization[name], 9),
                "limit": deployment.transport_budgets.get(name, 0.8),
            }
            for name in sorted(transport_by_name)
        },
    }
    hardware_lock = {
        host_name: {
            "profile": profile.metadata.name,
            "platform": profile.platform,
            "board": profile.board,
            "resources": {
                resource.name: {
                    "kind": resource.kind,
                    "backend": resource.backend,
                    "device": resource.device,
                    "options": dict(sorted(resource.options.items())),
                }
                for resource in sorted(profile.resources, key=lambda item: item.name)
            },
        }
        for host_name, profile in sorted(hardware.items())
    }
    transports_lock = {
        transport.name: {
            "type": transport.type,
            "backend": transport.backend,
            "package": transport.package,
            "hosts": sorted(transport.hosts),
            "bitrate_bps": transport.bitrate_bps,
            "mtu": transport.mtu,
            "resource": transport.resource,
            "options": dict(sorted(transport.options.items())),
        }
        for transport in sorted(deployment.transports, key=lambda item: item.name)
    }

    package_manifests: dict[str, Path] = {}
    package_protos: dict[str, tuple[Path, ...]] = {}
    package_bounds: dict[str, Path | None] = {}
    for entry in workspace.packages:
        package_root = _resolve(workspace.source.parent, entry.source)
        manifest = (
            package_root if package_root.name.endswith(".yaml") else package_root / "package.yaml"
        )
        package_manifests[entry.name] = manifest
        package = load_package(manifest)
        package_protos[entry.name] = tuple(
            _resolve(manifest.parent, path) for path in package.exports.get("protos", ())
        )
        package_bounds[entry.name] = (
            _resolve(manifest.parent, package.protobuf.bounds)
            if package.protobuf is not None and package.protobuf.bounds is not None
            else None
        )
    artifacts: dict[str, Any] = {}
    for node_name, locked_node in nodes_lock.items():
        input_paths: dict[str, Path] = {
            "application": application.source,
            "deployment": deployment.source,
            "workspace": workspace.source,
        }
        for instance_name in locked_node["instances"]:
            instance = next(item for item in application.instances if item.name == instance_name)
            package_name = instance.module.split("/", 1)[0]
            module = module_by_instance[instance_name]
            input_paths[f"module:{instance.module}"] = module.source
            input_paths[f"package:{package_name}"] = package_manifests[package_name]
            for index, proto in enumerate(package_protos[package_name]):
                input_paths[f"proto:{package_name}:{index}"] = proto
            if package_bounds[package_name] is not None:
                input_paths[f"protobuf-bounds:{package_name}"] = package_bounds[package_name]
            if module.header:
                candidates = (
                    workspace.source.parent / module.header,
                    module.source.parent / module.header,
                )
                implementation = next(
                    (candidate.resolve() for candidate in candidates if candidate.is_file()),
                    None,
                )
                if implementation is not None:
                    input_paths[f"implementation:{instance.module}"] = implementation
        host_name = locked_node["host"]
        if host_name in hardware:
            input_paths[f"hardware:{host_name}"] = hardware[host_name].source
        inputs = [
            {"label": label, "digest": _file_digest(path)}
            for label, path in sorted(input_paths.items())
        ]
        node_routes = [
            route
            for route in routes
            if node_name in {route["source_node"], route["destination_node"]}
        ]
        node_capabilities = {
            instance_name: capability_bindings[instance_name]
            for instance_name in locked_node["instances"]
            if capability_bindings[instance_name]
        }
        node_transports = {
            name: transports_lock[name]
            for name in sorted(
                {route["transport"] for route in node_routes if route["transport"] != "local"}
            )
        }
        input_digest = hashlib.sha256(
            canonical_json(
                {
                    "deployment_id": deployment_id,
                    "node": locked_node,
                    "routes": node_routes,
                    "hardware": hardware_lock.get(host_name),
                    "capability_bindings": node_capabilities,
                    "transports": node_transports,
                    "resource_budgets": resource_budgets["hosts"][host_name],
                    "inputs": inputs,
                }
            )
        ).hexdigest()
        artifacts[node_name] = {
            "digest_kind": "inputs",
            "input_digest": input_digest,
            "artifact_digest": None,
            "inputs": inputs,
        }

    base = {
        "api_version": API_VERSION,
        "kind": "DeploymentLock",
        "metadata": {"name": deployment.metadata.name},
        "deployment_id": deployment_id,
        "application_hash": app_graph["content_hash"],
        "deployment_hash": deployment_hash,
        "nodes": nodes_lock,
        "routes": routes,
        "transports": transports_lock,
        "hardware": hardware_lock,
        "capability_bindings": {
            name: bindings for name, bindings in sorted(capability_bindings.items()) if bindings
        },
        "resource_budgets": resource_budgets,
        "artifacts": artifacts,
        "utilization": {name: round(value, 9) for name, value in sorted(utilization.items())},
        "stack_bytes": dict(sorted(stack_by_host.items())),
        "static_ram_bytes": dict(sorted(static_ram_by_host.items())),
        "runtime_ram_bytes": dict(sorted(runtime_ram_by_host.items())),
        "flash_bytes": dict(sorted(flash_by_host.items())),
        "hosts": {
            item.name: {"os": item.os, "arch": item.arch, "board": item.board}
            for item in sorted(deployment.hosts, key=lambda item: item.name)
        },
    }
    content = dict(base)
    content["content_hash"] = hashlib.sha256(canonical_json(base)).hexdigest()
    return content


def to_dot(graph: dict[str, Any]) -> str:
    lines = ["digraph application {", "  rankdir=LR;", f'  label="{graph["name"]}";']
    for instance in graph["instances"]:
        lines.append(f'  "{instance["name"]}" [label="{instance["name"]}\\n{instance["module"]}"];')
    for connection in graph["connections"]:
        source, _ = _split_endpoint(connection["from"])
        destination, _ = _split_endpoint(connection["to"])
        lines.append(
            f'  "{source}" -> "{destination}" '
            f'[label="#{connection["id"]} {connection["kind"]} '
            f'{connection["type"]}"];'
        )
    for binding in graph["bindings"]:
        source, _ = _split_endpoint(binding["provider"])
        destination, _ = _split_endpoint(binding["requirement"])
        lines.append(f'  "{source}" -> "{destination}" [style=dashed,label="provider"];')
    for instance in graph["instances"]:
        for dependency in instance["startup_after"]:
            lines.append(
                f'  "{dependency}" -> "{instance["name"]}" [style=dotted,label="startup"];'
            )
    lines.append("}")
    return "\n".join(lines) + "\n"
