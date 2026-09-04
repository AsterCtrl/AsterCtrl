# Changelog

All notable changes are recorded here. The format follows Keep a Changelog and
the project follows Semantic Versioning.

## [Unreleased]

## [0.2.0-alpha.1] - 2026-09-04

### Added

- Native Linux and Zephyr runtime architecture.
- Application and Deployment Graph schema v1alpha2.
- Public `aster` command and `aster_cli` Python package.
- Versioned C ABI for Linux Module Bundles and Core Plugins.
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
