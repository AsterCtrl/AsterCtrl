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
- SocketCAN Adapter lifecycle coverage on Linux ``vcan`` and explicit CAN
  transport state in the Zephyr RAM budget.

### Fixed

- Release SBOMs now inventory every packaged asset by path, size, and SHA-256
  instead of reporting only the staging directory.
- Linux consumers using a GNU C++ dialect no longer collide with the compiler's
  legacy ``linux`` preprocessor macro.

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
