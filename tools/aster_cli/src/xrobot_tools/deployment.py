"""Static deployment graph compiler and resource validation."""

from __future__ import annotations

import fnmatch
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from xrobot_tools.cpp_codegen import record_wire_sizes
from xrobot_tools.interface_model import (
    InterfaceError,
    InterfaceModel,
    canonical_bytes,
    hash16,
)
from xrobot_tools.validation import ValidationError, validate_document
from xrobot_tools.workspace_model import ModuleManifest, Workspace, WorkspaceError


class DeploymentError(ValueError):
    """Raised when a deployment cannot be compiled safely."""


@dataclass(frozen=True)
class Instance:
    name: str
    package: str
    module: str
    config: dict[str, Any]
    manifest: ModuleManifest
    node: str


@dataclass(frozen=True)
class PortEndpoint:
    binding: str
    instance: str
    node: str
    port: str
    kind: str
    type_name: str


@dataclass(frozen=True)
class Route:
    name: str
    kind: str
    type_name: str
    type_hash: str
    source_node: str
    destination_nodes: tuple[str, ...]
    link: str
    qos: str
    max_rate_hz: float
    max_serialized_size: int
    frame_count: int
    bits_per_message: int


@dataclass(frozen=True)
class LinkBudget:
    name: str
    bitrate_bps: int
    reserved_utilization: float
    route_utilization: float
    total_utilization: float
    utilization_limit: float

    @property
    def within_budget(self) -> bool:
        return self.total_utilization <= self.utilization_limit


@dataclass(frozen=True)
class DeploymentPlan:
    name: str
    deployment_hash: str
    schema_hash: str
    deployment: dict[str, Any]
    robot: dict[str, Any]
    instances: tuple[Instance, ...]
    hardware: dict[str, dict[str, Any]]
    routes: tuple[Route, ...]
    link_budgets: tuple[LinkBudget, ...]
    type_hashes: dict[str, str]


@dataclass(frozen=True)
class DeploymentResult:
    node_count: int
    instance_count: int
    cross_node_route_count: int
    deployment_hash: str
    output_dir: Path


def _resolve_path(base: Path, value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (base / path).resolve()


def _place_instances(
    robot: dict[str, Any], deployment: dict[str, Any]
) -> dict[str, str]:
    declared = set(robot["instances"])
    placed: dict[str, str] = {}
    for node_name, node in deployment["nodes"].items():
        for instance in node["instances"]:
            if instance not in declared:
                raise DeploymentError(
                    f"node {node_name!r} places unknown instance {instance!r}"
                )
            if instance in placed:
                raise DeploymentError(
                    f"instance {instance!r} is placed more than once "
                    f"({placed[instance]!r} and {node_name!r})"
                )
            placed[instance] = node_name
    missing = sorted(declared - set(placed))
    if missing:
        raise DeploymentError(f"instances are not placed: {', '.join(missing)}")
    authority = deployment.get("time_authority")
    if authority is not None and authority not in deployment["nodes"]:
        raise DeploymentError(f"time authority node {authority!r} does not exist")
    return placed


def _load_hardware(
    deployment_path: Path, deployment: dict[str, Any]
) -> dict[str, dict[str, Any]]:
    profiles: dict[str, dict[str, Any]] = {}
    for node_name, node in deployment["nodes"].items():
        hardware_path = _resolve_path(
            deployment_path.parent, node["target"]["hardware"]
        )
        profiles[node_name] = validate_document(hardware_path)
    for link_name, link in deployment["links"].items():
        endpoint_nodes: set[str] = set()
        for endpoint in link["endpoints"]:
            node_name = endpoint["node"]
            if node_name not in profiles:
                raise DeploymentError(
                    f"link {link_name!r} references unknown node {node_name!r}"
                )
            if node_name in endpoint_nodes:
                raise DeploymentError(
                    f"link {link_name!r} repeats endpoint node {node_name!r}"
                )
            endpoint_nodes.add(node_name)
            resources = profiles[node_name]["spec"]["resources"]
            resource_name = endpoint["resource"]
            if resource_name not in resources:
                raise DeploymentError(
                    f"link {link_name!r}: node {node_name!r} has no resource "
                    f"{resource_name!r}"
                )
            if link["transport"] == "xrobot-can" and resources[resource_name]["kind"] != "can":
                raise DeploymentError(
                    f"link {link_name!r}: resource {resource_name!r} is not CAN"
                )
    return profiles


def _load_instances(
    workspace: Workspace,
    robot: dict[str, Any],
    placements: dict[str, str],
    hardware: dict[str, dict[str, Any]],
) -> tuple[Instance, ...]:
    instances: list[Instance] = []
    for name in sorted(robot["instances"]):
        config = robot["instances"][name]
        manifest = workspace.module(config["package"], config["module"])
        node = placements[name]
        required_hardware = manifest.document["spec"].get("hardware", [])
        bindings = config.get("hardware", {})
        profile = hardware[node]["spec"]
        available = set(profile["resources"]) | set(profile.get("devices", {}))
        for requirement in required_hardware:
            if requirement not in bindings:
                raise DeploymentError(
                    f"instance {name!r} does not bind hardware requirement "
                    f"{requirement!r}"
                )
            if bindings[requirement] not in available:
                raise DeploymentError(
                    f"instance {name!r} binds {requirement!r} to unknown hardware "
                    f"{bindings[requirement]!r} on node {node!r}"
                )
        instances.append(
            Instance(
                name=name,
                package=config["package"],
                module=config["module"],
                config=config,
                manifest=manifest,
                node=node,
            )
        )
    return tuple(instances)


def _collect_ports(instances: tuple[Instance, ...]) -> dict[str, list[PortEndpoint]]:
    bindings: dict[str, list[PortEndpoint]] = {}
    for instance in instances:
        configured = instance.config.get("ports", {})
        manifest_ports = {
            port["name"]: port for port in instance.manifest.document["spec"]["ports"]
        }
        unknown = sorted(set(configured) - set(manifest_ports))
        if unknown:
            raise DeploymentError(
                f"instance {instance.name!r} configures unknown ports: {', '.join(unknown)}"
            )
        for port_name, port in manifest_ports.items():
            if port_name not in configured:
                if port.get("required", True):
                    raise DeploymentError(
                        f"instance {instance.name!r} does not bind required port {port_name!r}"
                    )
                continue
            binding = configured[port_name]
            bindings.setdefault(binding, []).append(
                PortEndpoint(
                    binding=binding,
                    instance=instance.name,
                    node=instance.node,
                    port=port_name,
                    kind=port["kind"],
                    type_name=port["type"],
                )
            )
    return bindings


def _route_role(
    name: str, endpoints: list[PortEndpoint]
) -> tuple[str, PortEndpoint, tuple[PortEndpoint, ...]]:
    types = {endpoint.type_name for endpoint in endpoints}
    if len(types) != 1:
        raise DeploymentError(
            f"binding {name!r} has incompatible types: {', '.join(sorted(types))}"
        )
    kinds = {endpoint.kind for endpoint in endpoints}
    if kinds <= {"publisher", "subscriber"}:
        sources = [item for item in endpoints if item.kind == "publisher"]
        destinations = tuple(item for item in endpoints if item.kind == "subscriber")
        kind = "topic"
    elif kinds <= {"service_client", "service_server"}:
        sources = [item for item in endpoints if item.kind == "service_server"]
        destinations = tuple(item for item in endpoints if item.kind == "service_client")
        kind = "service"
    elif kinds <= {"action_client", "action_server"}:
        sources = [item for item in endpoints if item.kind == "action_server"]
        destinations = tuple(item for item in endpoints if item.kind == "action_client")
        kind = "action"
    else:
        raise DeploymentError(f"binding {name!r} mixes incompatible port roles")
    if len(sources) != 1:
        raise DeploymentError(
            f"binding {name!r} requires exactly one {kind} source/server"
        )
    if not destinations:
        return kind, sources[0], ()
    return kind, sources[0], destinations


def _type_contract(
    kind: str, type_name: str, model: InterfaceModel, sizes: dict[str, int]
) -> tuple[str, list[int]]:
    if kind == "topic":
        if type_name not in model.records:
            raise DeploymentError(f"Topic type {type_name!r} is not a generated Message")
        record = model.records[type_name]
        return record.schema_hash, [sizes[type_name]]
    interface = next(
        (
            item
            for item in model.interfaces
            if item.full_name == type_name and item.kind.lower() == kind
        ),
        None,
    )
    if interface is None:
        raise DeploymentError(f"{kind.title()} type {type_name!r} is not generated")
    return interface.schema_hash, [sizes[item.full_name] for item in interface.records]


def _select_qos(
    route_name: str, deployment: dict[str, Any]
) -> tuple[str, dict[str, Any], str | None]:
    for rule in deployment["route_rules"]:
        if fnmatch.fnmatchcase(route_name, rule["match"]["topic"]):
            qos_name = rule["qos"]
            if qos_name not in deployment["qos_profiles"]:
                raise DeploymentError(
                    f"route {route_name!r} selects unknown QoS {qos_name!r}"
                )
            qos = deployment["qos_profiles"][qos_name]
            if "max_rate_hz" not in qos:
                raise DeploymentError(
                    f"cross-node route {route_name!r} must declare max_rate_hz"
                )
            if qos["class"] == "control":
                required = ("deadline_ms", "max_age_ms", "on_stale", "rearm")
                missing = [key for key in required if key not in qos]
                if missing:
                    raise DeploymentError(
                        f"control route {route_name!r} is missing {', '.join(missing)}"
                    )
                if qos.get("adaptive", False):
                    raise DeploymentError(
                        f"control route {route_name!r} cannot use adaptive throttling"
                    )
            return qos_name, qos, rule.get("via")
    raise DeploymentError(f"cross-node route {route_name!r} has no QoS rule")


def _select_link(
    route_name: str,
    nodes: set[str],
    deployment: dict[str, Any],
    via: str | None,
) -> tuple[str, dict[str, Any]]:
    candidates = []
    for name, link in deployment["links"].items():
        link_nodes = {endpoint["node"] for endpoint in link["endpoints"]}
        if nodes <= link_nodes:
            candidates.append((name, link))
    if via is not None:
        candidates = [candidate for candidate in candidates if candidate[0] == via]
    if len(candidates) != 1:
        names = ", ".join(name for name, _ in candidates) or "none"
        raise DeploymentError(
            f"route {route_name!r} requires one physical link, candidates: {names}"
        )
    return candidates[0]


def _classic_frame_bits(payload_bytes: int) -> int:
    stuffed_region = 34 + payload_bytes * 8
    worst_stuff_bits = (stuffed_region - 1) // 4
    return stuffed_region + worst_stuff_bits + 13


def _can_cost(record_sizes: list[int], mtu: int) -> tuple[int, int]:
    frame_count = 0
    total_bits = 0
    for size in record_sizes:
        if size <= mtu - 1:
            payloads = [size + 1]
        else:
            fragment_payload = mtu - 2
            if fragment_payload <= 0:
                raise DeploymentError("CAN MTU is too small for fragmentation headers")
            payloads = []
            remaining = size
            while remaining > 0:
                chunk = min(fragment_payload, remaining)
                payloads.append(chunk + 2)
                remaining -= chunk
        frame_count += len(payloads)
        total_bits += sum(_classic_frame_bits(payload) for payload in payloads)
    return frame_count, total_bits


def _compile_routes(
    bindings: dict[str, list[PortEndpoint]],
    deployment: dict[str, Any],
    model: InterfaceModel,
) -> tuple[Route, ...]:
    sizes = record_wire_sizes(model)
    routes: list[Route] = []
    for name in sorted(bindings):
        kind, source, destinations = _route_role(name, bindings[name])
        remote = tuple(
            item for item in destinations if item.node != source.node
        )
        if not remote:
            continue
        destination_nodes = tuple(sorted({item.node for item in remote}))
        type_hash, record_sizes = _type_contract(
            kind, source.type_name, model, sizes
        )
        qos_name, qos, via = _select_qos(name, deployment)
        link_name, link = _select_link(
            name, {source.node, *destination_nodes}, deployment, via
        )
        if link["transport"] != "xrobot-can":
            raise DeploymentError(
                f"transport {link['transport']!r} is not implemented for route {name!r}"
            )
        options = link["options"]
        if options.get("frame") != "classic":
            raise DeploymentError("only classic CAN is implemented in v1alpha1")
        mtu = int(options.get("mtu_bytes", 8))
        frame_count, bits = _can_cost(record_sizes, mtu)
        routes.append(
            Route(
                name=name,
                kind=kind,
                type_name=source.type_name,
                type_hash=type_hash,
                source_node=source.node,
                destination_nodes=destination_nodes,
                link=link_name,
                qos=qos_name,
                max_rate_hz=float(qos["max_rate_hz"]),
                max_serialized_size=max(record_sizes),
                frame_count=frame_count,
                bits_per_message=bits,
            )
        )
    return tuple(routes)


def _compile_budgets(
    deployment: dict[str, Any], routes: tuple[Route, ...]
) -> tuple[LinkBudget, ...]:
    reserved = deployment.get("reserved_bandwidth", {})
    budgets: list[LinkBudget] = []
    for name in sorted(deployment["links"]):
        link = deployment["links"][name]
        if link["transport"] != "xrobot-can":
            continue
        bitrate = int(link["options"].get("bitrate_bps", 0))
        if bitrate <= 0:
            raise DeploymentError(f"link {name!r} must declare bitrate_bps")
        route_bits_per_second = sum(
            route.bits_per_message * route.max_rate_hz
            for route in routes
            if route.link == name
        )
        route_utilization = route_bits_per_second / bitrate
        reserved_utilization = float(reserved.get(name, 0.0))
        limit = float(link["budget"]["utilization_limit"])
        budget = LinkBudget(
            name=name,
            bitrate_bps=bitrate,
            reserved_utilization=reserved_utilization,
            route_utilization=route_utilization,
            total_utilization=reserved_utilization + route_utilization,
            utilization_limit=limit,
        )
        if not budget.within_budget:
            raise DeploymentError(
                f"link {name!r} utilization {budget.total_utilization:.6f} "
                f"exceeds limit {limit:.6f}"
            )
        budgets.append(budget)
    return tuple(budgets)


def _build_plan(
    workspace_path: Path, deployment_path: Path
) -> DeploymentPlan:
    workspace = Workspace(workspace_path)
    deployment = validate_document(deployment_path)
    robot_path = _resolve_path(deployment_path.parent, deployment["application"])
    robot = validate_document(robot_path)
    placements = _place_instances(robot, deployment)
    hardware = _load_hardware(deployment_path, deployment)
    instances = _load_instances(workspace, robot, placements, hardware)
    bindings = _collect_ports(instances)
    model = workspace.interface_model()
    routes = _compile_routes(bindings, deployment, model)
    budgets = _compile_budgets(deployment, routes)
    type_hashes = {route.type_name: route.type_hash for route in routes}
    deployment_hash = hash16(
        {
            "deployment": deployment,
            "robot": robot,
            "modules": [item.manifest.document for item in instances],
            "hardware": hardware,
            "schema_hash": model.deployment_schema_hash,
        }
    )
    return DeploymentPlan(
        name=deployment["metadata"]["name"],
        deployment_hash=deployment_hash,
        schema_hash=model.deployment_schema_hash,
        deployment=deployment,
        robot=robot,
        instances=instances,
        hardware=hardware,
        routes=routes,
        link_budgets=budgets,
        type_hashes=type_hashes,
    )


def compile_deployment(
    workspace_path: str | Path,
    deployment_path: str | Path,
    output_dir: str | Path,
) -> DeploymentResult:
    workspace = Path(workspace_path).resolve()
    deployment = Path(deployment_path).resolve()
    output = Path(output_dir).resolve()
    try:
        plan = _build_plan(workspace, deployment)
        from xrobot_tools.deployment_output import write_deployment

        write_deployment(plan, output)
    except (ValidationError, WorkspaceError, InterfaceError) as error:
        raise DeploymentError(str(error)) from error
    return DeploymentResult(
        node_count=len(plan.deployment["nodes"]),
        instance_count=len(plan.instances),
        cross_node_route_count=len(plan.routes),
        deployment_hash=plan.deployment_hash,
        output_dir=output,
    )
