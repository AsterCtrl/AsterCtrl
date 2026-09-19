# Linux Runtime

## Default workflow

`aster run --config runtime.yaml` starts aster_runtime without a custom launcher.
Linux has only the user-facing `runtime.yaml`; Application files and Deployment Locks
are not local-run entrypoints. Packages, instances, executors, logging and parameters
are parsed at startup. Restart after configuration changes; hot reload
is not implemented.

Named serial executors and thread pools honor thread counts and queue capacities.
Local Channel/RPC resolve instance namespace/remap and asynchronously deliver owned
messages on assigned executors. Only `backends: [local]` is currently wired into this
launcher; CAN/USB and production CorePlugin integration are pending.

`aster validate runtime.yaml` checks configuration without loading Packages.
`aster run --config runtime.yaml --check` constructs and initializes Modules and
seals registrations: it executes application Initialize/Shutdown and is not a
static-only check. Both accept `--runtime PATH`.

## Embedding and lifecycle

Include aster_runtime/platform/linux/runtime.hpp and link `aster::linux`.
Construct NodeRuntime, RegisterModule for borrowed static objects, then Initialize,
Start and Shutdown. Static and dynamic instances may coexist. Borrowed Modules must
outlive the Runtime. Business Packages link only public Interfaces.

SIGINT/SIGTERM request shutdown. The Runtime closes admission, quiesces executors,
waits for in-flight callbacks, cancels outstanding RPCs, stops Modules in reverse
order, destroys instances and unloads libraries. Shutdown belongs on the controlling
thread; callbacks must not wait for their own Runtime to stop.

State, diagnostics and Module-instance enumeration exist. Complete communication
graph inspection, distributed health monitoring, configured Clock replacement and
CorePlugin lifecycle do not. Loader tests do not establish those features.

## Service deployment

Linux installation provides aster-node@.service. It directly starts aster_runtime
with /opt/aster/<instance>/current/runtime.yaml. Its executable path comes from
CMAKE_INSTALL_PREFIX at CMake configuration time; reconfigure or adjust the unit
when installing to another prefix. Operators supply accounts, permissions, device
access and service activation.

Existing deploy apply verifies, stages and switches artifact directories.
**It does not call systemctl or flash an MCU.** It still consumes legacy deployment
inputs; v1alpha3 artifact-description integration remains pending.
