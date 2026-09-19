# Changelog

All notable changes are recorded here. The format follows Keep a Changelog and
the project follows Semantic Versioning.

## [Unreleased]

### Added

- Shared bounded Channel-to-packet Transport bridges and a lifecycle Module for
  generated Transport infrastructure.
- Generated Zephyr USB CDC ACM Channel wiring with bounded polling and explicit
  framing-buffer memory accounting.
- A generated Linux static-node owner that supplies per-Instance CoreRef values,
  owns the Supervisor and Runtime services, and performs signal-driven shutdown.
- Generated Linux TTY/USB CDC ACM Channel wiring plus a pseudo-TTY startup,
  framing and shutdown acceptance test.
- Generated lifecycle-managed CAN/SocketCAN Channel wiring with bounded
  best-effort and reliable paths, handshake/time-sync gating, retry and peer
  restart recovery.
- Generated CAN RPC client/server wiring through a node-level RPC router, with
  bounded request/response state, peer-restart recovery and typed service
  descriptors shared by Linux and Zephyr.
- SocketCAN Adapter lifecycle coverage on Linux ``vcan`` and explicit CAN
  transport state in the Zephyr RAM budget.
- Generated Linux Module Package translation units and shared-library targets
  from declarative Package and Module manifests.
- A simplified-Chinese Sphinx documentation tree and a curated API reference
  extracted as Doxygen XML and rendered through Breathe.

### Changed

- The default Linux workflow is now `runtime.yaml` plus `aster run`; ordinary
  applications no longer require an Application/Deployment graph pair.
- Legacy v1alpha2 Graph, Provider and generated CAN/USB paths are documented as
  migration regressions while the v1alpha3 deployment compiler is implemented.
- Technical documentation now uses MyST Markdown throughout while retaining
  Sphinx HTML and link validation, matching the contribution workflow used by
  the AimRT reference project.
- Static and dynamic Modules now share the canonical `aster_module_base_t`
  lifecycle path. `ModuleBase` and `CoreRef` are thin C++ facades, while
  Runtime service adaptation is owned by `CoreAdapter` rather than the plugin
  loader.
- Canonical C ABI tables and their C++ service facades now live in component
  headers; the former monolithic headers remain as convenience imports only.

### Fixed

- Native `aster validate runtime.yaml` now parses configuration without loading
  Packages; `aster run --check` remains the explicit registration check.
- Dynamic Package loading rejects and destroys a factory result whose reported
  Module type differs from the requested exported type.
- The generated systemd unit starts the installed `aster_runtime` with
  `runtime.yaml` instead of the removed `aster run --execute` command.
- Release SBOMs now inventory every packaged asset by path, size, and SHA-256
  instead of reporting only the staging directory.
- Linux consumers using a GNU C++ dialect no longer collide with the compiler's
  legacy ``linux`` preprocessor macro.

## [0.2.0-alpha.1] - 2026-09-04

### Added

- Native Linux and Zephyr runtime architecture.
- Application and Deployment Graph schema v1alpha2.
- Public `aster` command and `aster_cli` Python package.
- Versioned C ABI for Linux Module Packages and Core Plugins.
- Bounded Protobuf profile and deterministic graph locks.
- Fixed-capacity Zephyr node owner for Runtime lifecycle and core services.
- Zephyr CAN Device Adapter with bounded ISR-to-thread receive handoff.
- Build-time classic-CAN Route ID, fragmentation and transmit-queue bounds.

### Known limitations

- The alpha cross-node firmware is compile-only. Generated CAN/USB route-bridge
  construction and physical-board data-link validation remain release work.

### Removed

- XRobot, libxr, FreeRTOS and AimRT runtime dependencies.
- First-class Action abstraction; long-running workflows use Channel and RPC.
