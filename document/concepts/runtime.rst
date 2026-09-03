Runtime contract
================

Every business component implements the four-method ``aster::Module``
Interface:

.. code-block:: cpp

   ModuleInfo Info() const noexcept;
   Status Initialize(CoreRef core) noexcept;
   Status Start() noexcept;
   void Shutdown() noexcept;

``Initialize`` is the only registration phase. A Module obtains Configurator,
Logger, Executor, Channel, RPC, Parameter, Clock, Allocator and HardwareManager
handles from ``CoreRef`` and registers its endpoints. Once all Modules
initialize, the Runtime seals every Registry before calling ``Start``.

The lifecycle is load, initialize, seal, start, run and reverse-order shutdown.
An initialization, seal or start failure triggers reverse-order cleanup of all
initialized Modules. ``Status`` values have stable numeric categories for
configuration, resource, timeout, protocol, lifecycle and platform failures.

Execution rules
---------------

Portable Module code does not include operating-system headers. Linux uses
native threads and dynamic allocation where the Deployment permits them.
Zephyr executors use bounded message queues and fixed storage. Interrupt code
may enqueue a bounded ``WorkItem`` but must not invoke application callbacks,
allocate memory or block.

No C++ standard-library object, exception or ambiguous ownership crosses the
Linux plugin boundary. The C ABI identifies every table by ABI version and
structure size and gives every owned Module bundle an explicit release
function.
