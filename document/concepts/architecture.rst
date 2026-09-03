Architecture
============

The portable core is a deep Module: its small Interface hides lifecycle,
registration sealing, routing, capacity checks and rollback. Platform-specific
behaviour changes at Clock, Executor, Hardware and Transport Seams. Linux,
Zephyr and test fakes are Adapters at those Seams.

Business Modules never inspect the operating system. They receive a ``CoreRef``
during ``Initialize()``, register Channel/RPC endpoints, and acquire declared
hardware capabilities. Registration closes before ``Start()`` so Zephyr can use
fixed storage and deterministic routing.

AimRT and ROS interoperability belongs in optional Bridge packages. Neither is
part of the runtime dependency graph.
