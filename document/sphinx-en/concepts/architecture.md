# Architecture

AsterCtrl is an independent robot control framework, not another operating
system. Linux and Zephyr supply operating-system services. Portable Modules
reuse C++ source by recompiling for each platform, not by sharing one binary
between MCU and SoC.

Business Modules implement ModuleBase and obtain configuration, logging,
execution, Channel, RPC, parameters, Clock and Allocator through CoreRef.
Ordinary Linux uses runtime.yaml; optional cross-node/Zephyr deployment
compilation is still migrating. See [configuration and placement](graphs.md).

Algorithms communicate with Driver Modules through Channel/RPC. Device access
belongs inside Drivers; portable algorithms do not need generic hardware
Capabilities or HardwareManager. Legacy hardware registries and Provider
orchestration have not yet been fully removed from the migration path.

The public SDK in src/interface contains the canonical C ABI and thin C++
facades. Runtime, Supervisor, Registry, platform and Transport internals belong
to the separate Runtime SDK and implementation directories. Business Packages
do not have to link these implementations.

The framework owns lifecycle, registration sealing, scheduling, message
ownership and rollback. Linux may allocate dynamically. Zephyr must remain
bounded, exception-free and RTTI-free, without hot-path heap allocation.
The Linux core is implemented; v1alpha3 same-source Zephyr deployment still
needs verification. Design constraints are not hardware acceptance evidence.

AimRT is a design reference, not a dependency. ROS/AimRT Bridges, Linux IP
Transports, record/replay, distributed monitoring and full simulation/PIL
remain later work.
