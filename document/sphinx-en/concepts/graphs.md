# Configuration, communication and placement

Ordinary Linux applications no longer require an Application Graph, a Deployment
Graph, application.yaml, Module Port tables or a Deployment Lock.

## Implemented: runtime.yaml

The `aster` section selects Packages, Module instances, executors, logging,
communication backends and instance configuration:

```text
Write Module → build Package with CMake → runtime.yaml → aster run
```

Modules register Topics and RPCs during `Initialize()`. Names undergo namespace
expansion, one exact remap, then registration. Equal fully qualified Topics with
equal types broadcast; RPC targets use logical service-instance and method names.
Registries close before Modules start.

Linux parses YAML at startup using yaml-cpp and provides typed instance values.
Business configuration is not embedded as arbitrary JSON or object memory in
generated application code.

## Two internal views, not two mandatory user files

- **Communication relationships** come from actual registrations and cross-node contracts.
- **Placement relationships** come from instance placement, node platforms and links.

These are architectural responsibilities, not a claim that two complete Graph
inspection tools exist. NodeRuntime's `VisitGraph` currently enumerates Module
types and instances only. `aster graph` is still the legacy v1alpha2 Application
compiler, not the new Runtime's communication inspector.
Offline tools cannot infer arbitrary C++ registrations. `aster run --check`
loads Modules, initializes them and checks local registrations; cross-node
contract verification is still pending.

## Optional cross-node and Zephyr deployment: in progress

The target optional deployment.yaml references Runtime configuration and selects
placement, Linux/Zephyr platforms, boards, resources and links. Cross-node
contracts describe Topic/RPC names, types, sending/receiving nodes and capacities,
not Module Port-to-Port edges.

Business configuration remains in the Runtime configuration. Node configuration
contains platform policies, with no arbitrary deep YAML overrides. Zephyr needs
generated static entries, resource tables, Kconfig fragments and overlays rather
than an MCU YAML parser or open-ended topology discovery.
**The v1alpha3 deployment compiler is not complete.**

## Legacy double-graph implementation

Existing application.yaml, module.yaml Port declarations, Provider/Capability
orchestration, Hardware Profiles, the old resolver and several examples remain
for migration regression tests. They are neither the new Linux workflow nor a
second long-term architecture. Replacement paths must pass before their removal.

Use the [getting-started tutorial](../tutorials/getting-started.md).
See the [implementation audit](../development/convergence-audit.md) for gaps.
