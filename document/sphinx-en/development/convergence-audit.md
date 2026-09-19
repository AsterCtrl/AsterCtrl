# Dual-platform convergence: implementation audit

Audit date: 2026-09-05. Scope: the current uncommitted worktree, not a release
announcement or physical-board qualification.

## Conclusion

Ordinary Linux has a configuration-driven `Module → Package → runtime.yaml →
aster run` path without two user-authored graphs. **The implementation plan is
not complete:** v1alpha3 deployment compilation, Zephyr static generation and
cross-node backends for the new Runtime still lack an integrated path.

Communication relationships and placement remain internal-view concepts, not
mandatory Application/Port files. `VisitGraph` currently enumerates Modules;
the retained `aster graph` compiles v1alpha2 Applications. A complete live
communication view has not been delivered.

## Plan reconciliation

| Area | Implemented evidence | Remaining work |
| --- | --- | --- |
| C ABI and C++ facade | ABI 2, C-table-only Refs, Runtime outside the Module SDK, C/C++ contract tests | Continue platform error/ISR audits; an interface declaration does not prove a service exists |
| Packages and instances | Generated Linux entries, static/dynamic composition, independent configuration/log identity/names/executors, factory type validation | Zephyr static entries from the same Manifest |
| Host launcher and services | Native YAML, serial/pool executors, typed configuration/parameters, logging, Clock/Allocator, rollback and unload tests | Configurable Clock/test adapters; full health and communication inspection |
| Local Channel/RPC | Owned payloads, name resolution, broadcast, type conflicts, timeouts, backpressure, concurrency and shutdown tests | Cross-node registration verification is not implied by in-process tests |
| CorePlugin | Separate ABI, loader/version/interface-query tests | Production lifecycle and backend registration; `core_plugin_main.h` currently exposes query only |
| Deployment compilation | Runtime/Package use v1alpha3; old commands warn | `graph.py`, `models.py`, `emitters.py` and deployment schemas remain v1alpha2; new typed resolution and platform emission are absent |
| Zephyr | Retained bounded registries/executors/adapters and old generation | New typed static configuration, resource tables, entries and same-source execution; Parameter is unavailable; heap/real-ISR checks remain |
| CAN/USB | Retained CAN Channel/RPC, USB Channel, protocol and pseudo-TTY tests | New launcher accepts Local only; cross-node wiring and handshake integration remain; USB RPC is explicitly unsupported |
| Communication identity | Schema analysis and bounded encoding | Old emitter uses whole `lock["content_hash"]` as deployment handshake identity; business/log configuration and artifact integrity are not separated |
| Hardware simplification | New Host does not require algorithm HardwareManager use | Old Hardware/Capability/Provider schemas, generation, SDK and examples must be removed after their replacement is tested |
| CLI/build | Minimal Linux init, Package generation, no bespoke dependency manager, `cmake/`, pinned/pre-fetched yaml-cpp, C++20 exports and installed consumer | Explicit Zephyr/cross-node init options; `aster build` still consumes the old deployment model |
| Deployment operations | Bundle digests, staging, current/previous switching, systemd template | Still depends on old Deployment/Inventory rather than only built artifact descriptions; no service start or MCU flashing |
| Documentation/release | Doxygen XML embedded in bilingual Sphinx; current-vs-legacy guides corrected | Website migration; actual Linux/Zephyr CI and board gates; non-Python dependency SBOM coverage including yaml-cpp |

## Omissions fixed in this continuation

- `aster validate runtime.yaml` now uses the native parser without loading
  Packages. Previously it rejected the default init output. `run --check` is
  the separate Module initialization/registration check.
- The systemd template no longer invokes removed `run --execute` syntax.
  CMake fills in the installed `aster_runtime --config` command, which is
  exercised by an integration test. No systemd manager was run on this host.
- A factory returning a different `Info().type` is rejected with `TypeMismatch`
  and destroyed, preserving existing valid instances. The test first reproduced
  incorrect acceptance, then verified rejection.
- Bilingual concepts, Package, platform, deployment, debugging, protocol, API
  and CLI pages no longer describe old graphs/Providers/unwired plugins as the
  default workflow. Retained examples are marked as regressions.
- Old graph/resolve/build commands print migration warnings. Historical logs
  remain intact and their superseded architecture is labeled as historical.

## Verification limits

Host regressions ran on macOS, not Linux x86_64/arm64. Results belong in the
[development log](2026-09-05-core-convergence.md). SocketCAN tests skip the real
`vcan` path outside Linux; pseudo-TTY tests do not prove USB enumeration or
physical communication.

No usable Zephyr checkout/SDK was found locally. This continuation did not run
`native_sim`, QEMU, `dev_c`/`mc02` builds or size checks. Dated board console,
clock, CAN loopback, UART, SPI, watchdog and cross-node CAN loss/restart evidence
remain final-release gates.

A configured workflow is not a passing remote run. This iteration does not
commit, mutate remotes, publish, delete legacy archives or migrate robot business code.

## Next implementation order

1. Wire CorePlugin lifecycle and real backend registration; test rollback,
   in-flight shutdown and safe unloading.
2. Implement optional v1alpha3 Deployment with instance placement, named
   Topic/RPC node contracts, stable communication identity and pre-start checks
   against actual registrations, without Port-to-Port graphs.
3. Generate Zephyr static entries and bounded configuration/resources from the
   same Manifest; run the same Module source, then remove old Provider/Hardware
   graph compilation and duplicate examples.
4. Integrate CAN/USB and independent artifact Bundles; execute Linux/Zephyr
   build and hardware gates.

ROS Bridge, Linux IP transports, recording/replay, distributed monitoring and
full SIL/PIL are intentionally deferred, not missing scope to add in this iteration.
