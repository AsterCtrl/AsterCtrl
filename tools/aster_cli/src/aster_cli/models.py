"""Small typed model shared by validation, graph resolution, and emitters."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .validation import validate_document


@dataclass(frozen=True, slots=True)
class Metadata:
    name: str
    version: str | None = None
    labels: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class PackageDependency:
    name: str
    source: str
    version: str | None = None
    revision: str | None = None


@dataclass(frozen=True, slots=True)
class ProtobufConfig:
    bounds: str | None
    includes: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Package:
    metadata: Metadata
    build_system: str
    build_target: str | None
    exports: dict[str, tuple[str, ...]]
    dependencies: tuple[PackageDependency, ...]
    protobuf: ProtobufConfig | None
    source: Path


@dataclass(frozen=True, slots=True)
class Port:
    name: str
    kind: str
    type_name: str
    schema_hash: str | None = None
    required: bool = True
    max_rate_hz: float | None = None


@dataclass(frozen=True, slots=True)
class Provider:
    name: str
    interface: str
    domain: str | None = None


@dataclass(frozen=True, slots=True)
class Requirement:
    name: str
    interface: str
    domain: str | None = None
    optional: bool = False


@dataclass(frozen=True, slots=True)
class CapabilityRequirement:
    name: str
    kind: str
    optional: bool = False


@dataclass(frozen=True, slots=True)
class Task:
    name: str
    domain: str
    stack_bytes: int
    queue_depth: int
    period_us: int | None = None
    deadline_us: int | None = None
    priority: int = 0


@dataclass(frozen=True, slots=True)
class Module:
    metadata: Metadata
    target: str
    class_name: str | None
    header: str | None
    platforms: tuple[str, ...]
    parameters: dict[str, Any] | None
    static_ram_bytes: int
    flash_bytes: int
    ports: tuple[Port, ...]
    providers: tuple[Provider, ...]
    requirements: tuple[Requirement, ...]
    capabilities: tuple[CapabilityRequirement, ...]
    tasks: tuple[Task, ...]
    source: Path


@dataclass(frozen=True, slots=True)
class Instance:
    name: str
    module: str
    config: dict[str, Any]
    startup_after: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Connection:
    source: str
    destination: str
    route_id: int | None = None
    qos: str = "best_effort"
    max_rate_hz: float = 1.0
    max_size: int | None = None


@dataclass(frozen=True, slots=True)
class Binding:
    requirement: str
    provider: str


@dataclass(frozen=True, slots=True)
class Domain:
    name: str
    time: str


@dataclass(frozen=True, slots=True)
class Application:
    metadata: Metadata
    instances: tuple[Instance, ...]
    connections: tuple[Connection, ...]
    bindings: tuple[Binding, ...]
    domains: tuple[Domain, ...]
    source: Path


@dataclass(frozen=True, slots=True)
class Resource:
    name: str
    kind: str
    backend: str
    device: str
    options: dict[str, Any]


@dataclass(frozen=True, slots=True)
class Hardware:
    metadata: Metadata
    platform: str
    board: str | None
    resources: tuple[Resource, ...]
    flash_bytes: int | None
    ram_bytes: int | None
    source: Path


@dataclass(frozen=True, slots=True)
class Host:
    name: str
    os: str
    arch: str
    board: str | None
    hardware: str | None
    inventory: str | None


@dataclass(frozen=True, slots=True)
class ExecutorPolicy:
    domain: str
    policy: str
    workers: int | None
    priority: int | None
    stack_bytes: int | None
    queue_depth: int | None


@dataclass(frozen=True, slots=True)
class Node:
    name: str
    node_id: int | None
    host: str
    instances: tuple[str, ...]
    domains: tuple[str, ...]
    executors: tuple[ExecutorPolicy, ...]


@dataclass(frozen=True, slots=True)
class Transport:
    name: str
    type: str
    backend: str | None
    package: str | None
    hosts: tuple[str, ...]
    bitrate_bps: int | None
    mtu: int
    resource: str | None
    options: dict[str, Any]


@dataclass(frozen=True, slots=True)
class RouteRule:
    match: str
    transport: str
    qos: str | None


@dataclass(frozen=True, slots=True)
class TimeDomain:
    name: str
    source: str
    authority: str | None
    max_skew_us: int | None


@dataclass(frozen=True, slots=True)
class Deployment:
    metadata: Metadata
    application: str
    hosts: tuple[Host, ...]
    nodes: tuple[Node, ...]
    transports: tuple[Transport, ...]
    route_rules: tuple[RouteRule, ...]
    time_authority: str | None
    time_domains: tuple[TimeDomain, ...]
    host_budgets: dict[str, dict[str, int]]
    transport_budgets: dict[str, float]
    source: Path


@dataclass(frozen=True, slots=True)
class InventoryHost:
    name: str
    transport: str
    address: str | None
    serial_number: str | None
    deploy_root: str | None
    labels: dict[str, str]


@dataclass(frozen=True, slots=True)
class Inventory:
    metadata: Metadata
    hosts: tuple[InventoryHost, ...]
    source: Path


@dataclass(frozen=True, slots=True)
class WorkspacePackage:
    name: str
    source: str
    version: str | None
    revision: str | None


@dataclass(frozen=True, slots=True)
class Workspace:
    metadata: Metadata
    packages: tuple[WorkspacePackage, ...]
    source: Path


@dataclass(frozen=True, slots=True)
class LockedPackage:
    name: str
    source: str
    version: str
    revision: str
    digest: str


@dataclass(frozen=True, slots=True)
class PackageLock:
    metadata: Metadata
    packages: tuple[LockedPackage, ...]
    content_hash: str
    source: Path


@dataclass(frozen=True, slots=True)
class LockedRoute:
    id: int
    source: str
    destination: str
    kind: str
    type_name: str
    schema_hash: str
    schema_input_digest: str
    schema_hash_source: str
    source_node: str
    destination_node: str
    transport: str
    qos: str
    max_size: int
    max_encoded_size: int
    max_rate_hz: float


@dataclass(frozen=True, slots=True)
class LockedExecutor:
    domain: str
    policy: str
    backend: str
    workers: int
    priority: int
    stack_bytes: int
    queue_depth: int


@dataclass(frozen=True, slots=True)
class LockedZephyrRuntime:
    module_capacity: int
    channel_capacity: int
    subscriber_capacity: int
    rpc_capacity: int
    route_capacity: int
    maximum_message_size: int
    executor_queue_depth: int
    can_tx_queue_depth: int
    transport_storage_bytes: int
    hardware_capacity: int
    executor_stack_bytes: int
    arena_bytes: int
    fixed_ram_bytes: int


@dataclass(frozen=True, slots=True)
class LockedNode:
    name: str
    node_id: int
    host: str
    instances: tuple[str, ...]
    executors: tuple[LockedExecutor, ...]
    runtime: LockedZephyrRuntime | None


@dataclass(frozen=True, slots=True)
class LockedHost:
    name: str
    os: str
    arch: str
    board: str | None


@dataclass(frozen=True, slots=True)
class LockedHardwareResource:
    name: str
    kind: str
    backend: str
    device: str
    options: dict[str, Any]


@dataclass(frozen=True, slots=True)
class LockedHardware:
    host: str
    profile: str
    platform: str
    board: str | None
    resources: tuple[LockedHardwareResource, ...]


@dataclass(frozen=True, slots=True)
class LockedCapabilityBinding:
    instance: str
    capability: str
    kind: str
    resource: str
    backend: str
    device: str
    options: dict[str, Any]


@dataclass(frozen=True, slots=True)
class LockedTransport:
    name: str
    type: str
    backend: str | None
    package: str | None
    hosts: tuple[str, ...]
    bitrate_bps: int | None
    mtu: int
    resource: str | None
    options: dict[str, Any]


@dataclass(frozen=True, slots=True)
class BudgetUsage:
    used: float
    limit: float | None


@dataclass(frozen=True, slots=True)
class LockedHostBudgets:
    name: str
    stack_bytes: BudgetUsage
    ram_bytes: BudgetUsage
    flash_bytes: BudgetUsage


@dataclass(frozen=True, slots=True)
class LockedTransportBudget:
    name: str
    used: float
    limit: float


@dataclass(frozen=True, slots=True)
class ArtifactInput:
    label: str
    digest: str


@dataclass(frozen=True, slots=True)
class LockedArtifact:
    node: str
    digest_kind: str
    input_digest: str
    artifact_digest: str | None
    inputs: tuple[ArtifactInput, ...]


@dataclass(frozen=True, slots=True)
class DeploymentLock:
    metadata: Metadata
    deployment_id: str
    application_hash: str
    deployment_hash: str
    nodes: tuple[LockedNode, ...]
    routes: tuple[LockedRoute, ...]
    hosts: tuple[LockedHost, ...]
    hardware: tuple[LockedHardware, ...]
    capability_bindings: tuple[LockedCapabilityBinding, ...]
    transports: tuple[LockedTransport, ...]
    host_budgets: tuple[LockedHostBudgets, ...]
    transport_budgets: tuple[LockedTransportBudget, ...]
    artifacts: tuple[LockedArtifact, ...]
    utilization: dict[str, float]
    stack_bytes: dict[str, int]
    static_ram_bytes: dict[str, int]
    runtime_ram_bytes: dict[str, int]
    flash_bytes: dict[str, int]
    content_hash: str
    source: Path


def _metadata(document: dict[str, Any]) -> Metadata:
    item = document["metadata"]
    return Metadata(item["name"], item.get("version"), dict(item.get("labels", {})))


def _sequence(spec: dict[str, Any], name: str) -> list[dict[str, Any]]:
    return list(spec.get(name, []))


def load_package(path: str | Path) -> Package:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Package":
        raise ValueError(f"{source}: expected Package")
    spec = doc["spec"]
    dependencies: list[PackageDependency] = []
    for name, value in sorted(spec.get("dependencies", {}).items()):
        if isinstance(value, str):
            dependencies.append(PackageDependency(name, value))
        else:
            dependencies.append(
                PackageDependency(
                    name, value["source"], value.get("version"), value.get("revision")
                )
            )
    exports = {name: tuple(values) for name, values in sorted(spec.get("exports", {}).items())}
    protobuf_spec = spec.get("protobuf")
    protobuf = (
        ProtobufConfig(
            protobuf_spec.get("bounds"),
            tuple(protobuf_spec.get("includes", ())),
        )
        if protobuf_spec is not None
        else None
    )
    return Package(
        _metadata(doc),
        spec["build"]["system"],
        spec["build"].get("target"),
        exports,
        tuple(dependencies),
        protobuf,
        source,
    )


def load_module(path: str | Path) -> Module:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Module":
        raise ValueError(f"{source}: expected Module")
    spec = doc["spec"]
    ports = tuple(
        Port(
            p["name"],
            p["kind"],
            p["type"],
            p.get("schema_hash"),
            p.get("required", True),
            p.get("max_rate_hz"),
        )
        for p in _sequence(spec, "ports")
    )
    providers = tuple(
        Provider(p["name"], p["interface"], p.get("domain")) for p in _sequence(spec, "provides")
    )
    requirements = tuple(
        Requirement(r["name"], r["interface"], r.get("domain"), r.get("optional", False))
        for r in _sequence(spec, "requires")
    )
    capabilities = tuple(
        CapabilityRequirement(item["name"], item["kind"], item.get("optional", False))
        for item in _sequence(spec, "capabilities")
    )
    tasks = tuple(
        Task(
            t["name"],
            t["domain"],
            t["stack_bytes"],
            t["queue_depth"],
            t.get("period_us"),
            t.get("deadline_us"),
            t.get("priority", 0),
        )
        for t in _sequence(spec, "tasks")
    )
    impl = spec["implementation"]
    resources = spec.get("resources", {})
    return Module(
        _metadata(doc),
        impl["target"],
        impl.get("class"),
        impl.get("header"),
        tuple(spec.get("platforms", ())),
        dict(spec["parameters"]) if "parameters" in spec else None,
        int(resources.get("static_ram_bytes", 0)),
        int(resources.get("flash_bytes", 0)),
        ports,
        providers,
        requirements,
        capabilities,
        tasks,
        source,
    )


def load_application(path: str | Path) -> Application:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Application":
        raise ValueError(f"{source}: expected Application")
    spec = doc["spec"]
    instances = tuple(
        Instance(
            name,
            item["module"],
            dict(item.get("config", {})),
            tuple(item.get("startup_after", ())),
        )
        for name, item in sorted(spec["instances"].items())
    )
    connections = tuple(
        Connection(
            c["from"],
            c["to"],
            c.get("id"),
            c.get("qos", "best_effort"),
            float(c.get("max_rate_hz", 1.0)),
            int(c["max_size"]) if "max_size" in c else None,
        )
        for c in _sequence(spec, "connections")
    )
    bindings = tuple(Binding(b["requirement"], b["provider"]) for b in _sequence(spec, "bindings"))
    domains = tuple(Domain(d["name"], d["time"]) for d in _sequence(spec, "domains"))
    return Application(_metadata(doc), instances, connections, bindings, domains, source)


def load_hardware(path: str | Path) -> Hardware:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Hardware":
        raise ValueError(f"{source}: expected Hardware")
    spec = doc["spec"]
    resources = tuple(
        Resource(
            name,
            item["kind"],
            item["backend"],
            item["device"],
            dict(item.get("options", {})),
        )
        for name, item in sorted(spec["resources"].items())
    )
    memory = spec.get("memory", {})
    return Hardware(
        _metadata(doc),
        spec["platform"],
        spec.get("board"),
        resources,
        memory.get("flash_bytes"),
        memory.get("ram_bytes"),
        source,
    )


def load_deployment(path: str | Path) -> Deployment:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Deployment":
        raise ValueError(f"{source}: expected Deployment")
    spec = doc["spec"]
    hosts = tuple(
        Host(
            name,
            item["os"],
            item["arch"],
            item.get("board"),
            item.get("hardware"),
            item.get("inventory"),
        )
        for name, item in sorted(spec["hosts"].items())
    )
    nodes = tuple(
        Node(
            name,
            item.get("id"),
            item["host"],
            tuple(item["instances"]),
            tuple(item.get("domains", [])),
            tuple(
                ExecutorPolicy(
                    domain,
                    policy.get("policy", "serial"),
                    policy.get("workers"),
                    policy.get("priority"),
                    policy.get("stack_bytes"),
                    policy.get("queue_depth"),
                )
                for domain, policy in sorted(item.get("executors", {}).items())
            ),
        )
        for name, item in sorted(spec["nodes"].items())
    )
    transports = tuple(
        Transport(
            name,
            item["type"],
            item.get("backend"),
            item.get("package"),
            tuple(item["hosts"]),
            item.get("bitrate_bps"),
            int(
                item.get(
                    "mtu", 64 if item["type"] == "canfd" else 8 if item["type"] == "can" else 1500
                )
            ),
            item.get("resource"),
            dict(item.get("options", {})),
        )
        for name, item in sorted(spec.get("transports", {}).items())
    )
    rules = tuple(
        RouteRule(item["match"], item["transport"], item.get("qos"))
        for item in _sequence(spec, "route_rules")
    )
    time = spec.get("time", {})
    domains = tuple(
        TimeDomain(name, item["source"], item.get("authority"), item.get("max_skew_us"))
        for name, item in sorted(time.get("domains", {}).items())
    )
    budgets = spec.get("budgets", {})
    return Deployment(
        _metadata(doc),
        spec["application"],
        hosts,
        nodes,
        transports,
        rules,
        time.get("authority"),
        domains,
        dict(budgets.get("hosts", {})),
        {k: float(v) for k, v in budgets.get("transports", {}).items()},
        source,
    )


def load_inventory(path: str | Path) -> Inventory:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Inventory":
        raise ValueError(f"{source}: expected Inventory")
    hosts = tuple(
        InventoryHost(
            name,
            item["transport"],
            item.get("address"),
            item.get("serial_number"),
            item.get("deploy_root"),
            dict(item.get("labels", {})),
        )
        for name, item in sorted(doc["spec"]["hosts"].items())
    )
    return Inventory(_metadata(doc), hosts, source)


def load_workspace(path: str | Path) -> Workspace:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "Workspace":
        raise ValueError(f"{source}: expected Workspace")
    packages = tuple(
        WorkspacePackage(name, item["source"], item.get("version"), item.get("revision"))
        for name, item in sorted(doc["spec"]["packages"].items())
    )
    return Workspace(_metadata(doc), packages, source)


def load_package_lock(path: str | Path) -> PackageLock:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "PackageLock":
        raise ValueError(f"{source}: expected PackageLock")
    packages = tuple(
        LockedPackage(
            name,
            item["source"],
            item.get("version", "0.0.0"),
            item["revision"],
            item["digest"],
        )
        for name, item in sorted(doc["packages"].items())
    )
    return PackageLock(_metadata(doc), packages, doc["content_hash"], source)


def load_deployment_lock(path: str | Path) -> DeploymentLock:
    source = Path(path).resolve()
    doc = validate_document(source)
    if doc["kind"] != "DeploymentLock":
        raise ValueError(f"{source}: expected DeploymentLock")
    routes = tuple(
        LockedRoute(
            item["id"],
            item["from"],
            item["to"],
            item["kind"],
            item["type"],
            item["schema_hash"],
            item["schema_input_digest"],
            item["schema_hash_source"],
            item["source_node"],
            item["destination_node"],
            item["transport"],
            item["qos"],
            item["max_size"],
            item["max_encoded_size"],
            float(item["max_rate_hz"]),
        )
        for item in doc["routes"]
    )
    nodes = tuple(
        LockedNode(
            name,
            item["id"],
            item["host"],
            tuple(item["instances"]),
            tuple(
                LockedExecutor(
                    domain,
                    executor["policy"],
                    executor["backend"],
                    executor["workers"],
                    executor["priority"],
                    executor["stack_bytes"],
                    executor["queue_depth"],
                )
                for domain, executor in sorted(item.get("executors", {}).items())
            ),
            LockedZephyrRuntime(**item["runtime"]) if "runtime" in item else None,
        )
        for name, item in sorted(doc["nodes"].items())
    )
    hosts = tuple(
        LockedHost(name, item["os"], item["arch"], item.get("board"))
        for name, item in sorted(doc.get("hosts", {}).items())
    )
    hardware = tuple(
        LockedHardware(
            name,
            item["profile"],
            item["platform"],
            item.get("board"),
            tuple(
                LockedHardwareResource(
                    resource_name,
                    resource["kind"],
                    resource["backend"],
                    resource["device"],
                    dict(resource.get("options", {})),
                )
                for resource_name, resource in sorted(item["resources"].items())
            ),
        )
        for name, item in sorted(doc.get("hardware", {}).items())
    )
    capability_bindings = tuple(
        LockedCapabilityBinding(
            instance,
            capability,
            binding["kind"],
            binding["resource"],
            binding["backend"],
            binding["device"],
            dict(binding.get("options", {})),
        )
        for instance, bindings in sorted(doc.get("capability_bindings", {}).items())
        for capability, binding in sorted(bindings.items())
    )
    transports = tuple(
        LockedTransport(
            name,
            item["type"],
            item.get("backend"),
            item.get("package"),
            tuple(item["hosts"]),
            item.get("bitrate_bps"),
            item["mtu"],
            item.get("resource"),
            dict(item.get("options", {})),
        )
        for name, item in sorted(doc.get("transports", {}).items())
    )
    resource_budgets = doc["resource_budgets"]
    host_budgets = tuple(
        LockedHostBudgets(
            name,
            BudgetUsage(**item["stack_bytes"]),
            BudgetUsage(**item["ram_bytes"]),
            BudgetUsage(**item["flash_bytes"]),
        )
        for name, item in sorted(resource_budgets["hosts"].items())
    )
    transport_budgets = tuple(
        LockedTransportBudget(name, float(item["used"]), float(item["limit"]))
        for name, item in sorted(resource_budgets["transports"].items())
    )
    artifacts = tuple(
        LockedArtifact(
            name,
            item["digest_kind"],
            item["input_digest"],
            item["artifact_digest"],
            tuple(ArtifactInput(**source) for source in item["inputs"]),
        )
        for name, item in sorted(doc["artifacts"].items())
    )
    return DeploymentLock(
        _metadata(doc),
        doc["deployment_id"],
        doc["application_hash"],
        doc["deployment_hash"],
        nodes,
        routes,
        hosts,
        hardware,
        capability_bindings,
        transports,
        host_budgets,
        transport_budgets,
        artifacts,
        {name: float(value) for name, value in doc.get("utilization", {}).items()},
        {name: int(value) for name, value in doc.get("stack_bytes", {}).items()},
        {name: int(value) for name, value in doc.get("static_ram_bytes", {}).items()},
        {name: int(value) for name, value in doc.get("runtime_ram_bytes", {}).items()},
        {name: int(value) for name, value in doc.get("flash_bytes", {}).items()},
        doc["content_hash"],
        source,
    )
