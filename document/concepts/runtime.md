# Runtime contract

Every business component implements the four-method `aster::Module`
Interface:

```cpp
ModuleInfo Info() const noexcept;
Status Initialize(CoreRef core) noexcept;
Status Start() noexcept;
void Shutdown() noexcept;
```

`Initialize` is the only registration phase. A Module obtains Configurator,
Logger, Executor, Channel, RPC, Parameter, Clock, Allocator and HardwareManager
handles from `CoreRef` and registers its endpoints. Once all Modules
initialize, the Runtime seals every Registry before calling `Start`.

Each node exposes one RPC Interface to Application Modules. A bounded RPC
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
native threads and dynamic allocation where the Deployment permits them.
Zephyr executors use bounded message queues and fixed storage. Interrupt code
may enqueue a bounded `WorkItem` but must not invoke application callbacks,
allocate memory or block.

Linux and Zephyr use the same executor gate. `Initialize` and `Start` may
enqueue work, but the executor does not run it until every Module has started
successfully. Module lifecycle callbacks therefore must not wait for queued
work. Shutdown first rejects and purges queued work, wakes delayed waits and
joins the worker; only then does the Runtime call Module `Shutdown` in reverse
order. A running `WorkItem` must be finite and must not wait for Module
`Shutdown` or retain a lock that `Shutdown` needs. Lifecycle operations run
from the supervisor thread; asking an executor to shut itself down is a fatal
programming error.

Hardware resources needed during `Module::Initialize` are registered before
`StaticNodeRuntime::Initialize`. The generated binding table contains only
logical names, kinds, resources and Devicetree labels. Zephyr bootstrap calls
the generated helper, which checks the concrete device and borrows its pointer;
custom board infrastructure can use the same `RegisterHardware` seam.

The Zephyr v0.2 Core intentionally exposes an unavailable `ParameterRef`.
Immutable per-Instance values declared by `spec.parameters` remain supported
through the generated bounded `Configurator` JSON view. A mutable static
Parameter service requires a future explicit storage and update policy; the
Runtime does not pretend that one exists.

No C++ standard-library object, exception or ambiguous ownership crosses the
Linux plugin boundary. The C ABI identifies every table by ABI version and
structure size and gives every owned Module bundle an explicit release
function.
