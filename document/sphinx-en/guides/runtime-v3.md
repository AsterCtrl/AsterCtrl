# Configuration-driven Linux Runtime: migration status

This is an implementation note for the next alpha, not a full dual-platform acceptance statement.

## Available path

```text
C++ Module → Package shared library → runtime.yaml → aster run --config runtime.yaml
```

Ordinary Linux execution requires neither `application.yaml`, Module Port tables nor a
Deployment Lock. This illustrative configuration assumes `robot.Controller` has been built:

```yaml
api_version: aster.dev/v1alpha3
aster:
  executors:
    - {name: control, type: serial, queue_capacity: 64}
    - {name: io, type: thread_pool, threads: 2, queue_capacity: 128}
  logging: {level: info}
  channel: {backends: [local]}
  rpc: {backends: [local]}
  packages:
    - {name: robot, path: ./build/librobot.so}
  modules:
    - name: left
      type: robot.Controller
      package: robot
      executor: control
      namespace: /left
      remap: {/left/state: /robot/state}
      config: {gain: 2, label: "left motor"}
      parameters: {target: 0.0}
```

Library paths are relative to the configuration file. Disabled instances are not created.
Per-instance `log_level` overrides the default. Instance identity is separate from Module type.
The new launcher currently wires Local backends only; other backend names are rejected.

Embedding applications include `aster_runtime/platform/linux/runtime.hpp` and use
`aster::platform::linux::NodeRuntime`. Call `RegisterModule(instance_name, module)` before
`Initialize(yaml)`, `Start()` and `Shutdown()`. Borrowed static Modules omit `package` in
their configuration and must outlive the Runtime.

## Interface changes

- ABI 2 requires rebuilding old shared Packages.
- Module-facing Refs hold C function tables only. Backends, registries and transport
  implementations belong to the separate Runtime SDK.
- `configurator.Get("gain", value)` is typed. Missing keys return `NotFound`, mismatched
  types return `TypeMismatch`. Nested mappings use dotted paths such as `controller.gain`.
  Supported values are null, bool, 64-bit integers, finite floating point and strings.
  Arrays are rejected rather than silently serialized into strings.
- Immutable strings are borrowed until Shutdown. Mutable parameter payload reads copy
  into caller-provided storage; updates retain the original type and cannot add keys.
- Ordinary logging, publishing, RPC calls and executor posting need no explicit
  `ExecutionContext`.
- `clock.NowNs(now)` and `clock.GetDomain(domain)` return `Status`; failures leave output unchanged.

## Communication and shutdown

Names undergo namespace expansion, one exact remap and registration. Absolute names
skip namespace expansion. There are no wildcard or transitive remaps. Equal fully qualified
Topics with equal types broadcast to all subscribers; type conflicts fail initialization.

Channel copies encoded payloads once and shares ownership across queued deliveries.
Callbacks execute on each subscribing instance's executor. Full queues return
`CapacityExceeded`. Broadcast admission is per destination: failure does not retract
deliveries already accepted elsewhere, so retry is not an atomic broadcast transaction.

RPC distinguishes logical service instances from methods. Use
`client.Bind(core.rpc(), "right")`; an unspecified server instance defaults to its Module
instance in the new Runtime. Logical service instance names undergo namespace/remap.
Handlers execute on the server executor; normal completions and timeouts execute on the
client executor. A completion queue slot is reserved before accepting the request.

Shutdown closes communication admission, quiesces executors, discards queued work and
cancels outstanding RPCs before stopping Modules in reverse order and unloading Packages.
Cancellation callbacks run on the shutdown thread while Modules still exist. Shutdown
must be initiated by the controlling thread.

## Repository smoke check

`aster validate runtime.yaml --runtime /path/to/aster_runtime` checks configuration
through the native parser without loading Packages. `aster run --config runtime.yaml --check`
loads Packages, executes `Initialize()`, seals registrations and calls
`Shutdown()`; Module initialization may have side effects.

The [getting-started tutorial](../tutorials/getting-started.md) builds a minimal
user Package with `aster init` and `aster_add_package`. The following command
instead uses the repository's contract-test Package to exercise the launcher:

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
uv run aster run --runtime build/host/aster_runtime \
  --config build/host/configured-runtime-test.yaml --duration-ms 50
ctest --test-dir build/host --output-on-failure
```

yaml-cpp is fetched from an archive with a pinned SHA-256. To use pre-fetched source, set
`-DFETCHCONTENT_SOURCE_DIR_YAML_CPP=/path/to/yaml-cpp`, then configure with
`-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.

## Not complete yet

- Production CorePlugin lifecycle and selectable backend registration.
- Optional v1alpha3 Deployment compilation and same-source static Zephyr execution.
- CAN/USB wiring into the new launcher, communication identity and registration checks.
- Removal of generic Hardware/Capability/Provider orchestration and full website migration.
- Linux GCC, both Zephyr board builds and physical smoke tests.

The default template and Package entry use v1alpha3; the old dependency-management
commands are removed. Old deployment examples/commands remain for regression coverage, not as
a second supported architecture. They are not silently interpreted as v1alpha3. Hardware
and release gates must pass before claiming final-release acceptance.

See the [implementation audit](../development/convergence-audit.md) for code-level
plan reconciliation and the next implementation order.
