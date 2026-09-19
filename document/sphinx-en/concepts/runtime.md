# Runtime contract

Every business component implements the four-method `aster::ModuleBase`
Interface:

```cpp
ModuleInfo Info() const noexcept;
Status Initialize(CoreRef core);
Status Start();
void Shutdown() noexcept;
```

`Initialize` is the only registration phase. A Module obtains Configurator,
Logger, Executor, Channel, RPC, Parameter, Clock and Allocator
handles from `CoreRef` and registers its endpoints. Once all Modules
initialize, the Runtime seals every Registry before calling `Start`.

The configuration-driven Host exposes a scoped RPC Interface per instance;
see [runtime-v3](../guides/runtime-v3.md) for service identity and executor semantics.

In the retained v1alpha2 deployment path, a bounded RPC
router keeps local servers and local clients on the in-process backend while
directing a generated remote client to its Transport backend by the exact
service descriptor. Transport selection therefore remains a deployment concern:
the same Module calls `core.rpc()` in a local simulation or a CAN deployment.
The resolver rejects an ambiguous graph with two remote destinations for the
same service on one source node.

The lifecycle is load, initialize, seal, start, run and reverse-order shutdown.
An initialization, seal or start failure triggers reverse-order cleanup of all
initialized Modules. `Status` values have stable numeric categories for
configuration, resource, timeout, protocol, lifecycle and platform failures.

## Execution rules

Portable Module code does not include operating-system headers. Linux uses
native threads and dynamically sized queues configured in runtime.yaml.
Zephyr executors use bounded message queues and fixed storage. Interrupt code
may enqueue a bounded `WorkItem` but must not invoke application callbacks,
allocate memory or block.

The Host and retained Zephyr executors implement a start gate. This is not
evidence that the new v1alpha3 Zephyr deployment path is complete.
`Initialize` and `Start` may
enqueue work, but the executor does not run it until every Module has started
successfully. Module lifecycle callbacks therefore must not wait for queued
work. Shutdown first rejects and purges queued work, wakes delayed waits and
joins the worker; only then does the Runtime call Module `Shutdown` in reverse
order. A running `WorkItem` must be finite and must not wait for Module
`Shutdown` or retain a lock that `Shutdown` needs. Lifecycle operations run
from the supervisor thread; asking an executor to shut itself down is a fatal
programming error.

HardwareManager and generated hardware bootstrap remain in the v1alpha2
regression path. They are not a required portable algorithm contract, and the
new Host NodeRuntime does not provide a HardwareManager service. Driver Modules
own platform-specific hardware access.

The Host provides typed configuration and mutable parameters. The retained
Zephyr Core exposes an unavailable `ParameterRef`; its new typed static
configuration/parameter path still needs migration and bounded-storage tests.
See the [implementation audit](../development/convergence-audit.md).

No C++ standard-library object, exception or ambiguous ownership crosses the
Linux plugin boundary. Root C ABI tables carry an ABI version and structure
size; nested service tables carry a structure size and may only grow by
appending fields. Package factories pair every created Module with an explicit
destroy function.

## One lifecycle path

`aster::ModuleBase` is a convenience Interface, not a second Runtime contract. Its
base constructor embeds an `aster_module_base_t` handle whose function pointers are
small thunks to `Info`, `Initialize`, `Start` and `Shutdown`. The Runtime keeps
only an `aster::ModuleRef` and invokes that handle for both statically owned
Zephyr Modules and dynamically loaded Linux Modules. There is no parallel
"native C++" lifecycle path.

The reverse direction is equally narrow. `CoreAdapter` is the Runtime-side
Adapter that owns the C service tables. The public `CoreRef` contains only a
borrowed `aster_core_base_t` pointer and returns thin C++ views through its
typed accessors. Channel, RPC and Executor callbacks use the C callback shape as their
canonical carrier, so loading a Package does not allocate callback trampolines
or impose a hidden per-Module slot limit. The configuration-driven Host owns
separate service tables for each instance: configuration, logger identity,
namespace/remap and executor binding do not leak between instances. The old
generated `CoreRefOverlay` is not the new Host's instance-isolation mechanism.
