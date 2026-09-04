# Application and Deployment Graphs

`application.yaml` answers *what the robot does*: Module Instances, typed
ports, logical connections, behaviour parameters and capability requirements.
It contains no host, board, IP address, bus or pin.

`deployment.<environment>.yaml` answers *where and how it runs*: Node and Host
placement, Providers, boards, hardware profiles, links, transports, executors,
time policy and resource limits.

The resolver combines both into an immutable Deployment Lock. Runtime discovery
may verify a known peer but cannot invent Module Instances or routes. Real and
simulation deployments of one Application therefore retain the same logical
contract while selecting different Adapters.

Each Module owns a self-contained Draft 2020-12 JSON Schema under
`spec.parameters`. Instance `config` is validated before placement and is
emitted as deterministic canonical JSON for both Linux and Zephyr. Schemas may
use local fragment references but not external references, keeping validation
deterministic and available offline. The generated composition exposes those
bytes from the Instance-specific `CoreRef` Configurator under the `config`
key, without heap allocation or a Runtime YAML parser. A Module's
`static_ram_bytes` is counted once per placed Instance in addition to executor
stacks. For Zephyr, the Lock also records `runtime_ram_bytes`: a conservative
bound for the fixed arena, executor queue, Module and Registry slots, declared
Channel/subscriber and RPC capacities, message buffers, and Hardware registry.
`flash_bytes` is accumulated per Host, and all three RAM components and flash
are checked against the Deployment and Hardware Profile limits. Final linker
size checks remain authoritative.
Generated Zephyr packet Transports also contribute a conservative
`transport_storage_bytes` allowance to `runtime_ram_bytes` for framing
buffers, route bridges and lifecycle state; the Lock never treats this hidden
infrastructure as free memory.

Hardware Profiles are host-specific. A Zephyr profile maps logical resource
names to Devicetree node labels with the `devicetree` backend; a Linux profile
maps the same names to a SocketCAN interface or absolute TTY path. Official CAN
and USB links require compatible mappings at every endpoint. Zephyr `board`
values use the qualified Zephyr target form, for example
`dev_c/stm32f407xx`. Resolved resource options and every Module
capability-to-device binding are immutable parts of `deployment.lock.yaml`.
The generated composition exposes pointer-free `HardwareBindingDescriptor`
rows. On Zephyr, its registration helper resolves each already-validated node
label with `DEVICE_DT_GET`, checks `device_is_ready`, and registers the
borrowed device before `Initialize`. No address or ownership is serialized in
YAML or the Lock.

A Module may declare `capabilities` containing `name`, `kind` and an
optional flag. The resolver requires exactly one resource of that kind on the
placed host: zero providers fail a required capability and multiple providers
fail as ambiguous. Application code asks `HardwareManager` for the resolved
logical capability and never embeds a Devicetree label, SocketCAN interface or
TTY path.

A watchdog follows the same rule: a Module requests a `watchdog` capability,
while a Zephyr Hardware Profile maps it to a concrete `devicetree` watchdog
node such as `iwdg`.

For Channel and RPC connections, the Application names a protobuf message type
while its Package owns the exported `.proto` files and bounded profile. The
resolver derives the Schema Hash and maximum encoded size from those inputs.
`max_size` in a connection is an optional route-capacity override and must be
at least that derived maximum, so link budgets cannot silently undercount a
message that the generated TypeSupport can legally encode.
Automatic application Route IDs begin at 8 so the same Application can move to
classic CAN without colliding with its control-plane IDs. A Deployment still
validates every selected Transport's narrower ID and payload limits.

Executor policy belongs to the Deployment Graph, not the Module. A Module task
declares a domain plus bounded stack and queue requirements. A node may map that
domain to `serial` or, on Linux, `worker_pool`. Resolution checks the policy
against the placed tasks and records the concrete `linux_thread`,
`linux_worker_pool` or `zephyr_work_queue` backend in the lock. If omitted,
the deterministic default is one serial executor per enabled domain.
The v0.2 Zephyr Runtime owns one executor, so resolution rejects a Zephyr node
with more than one enabled domain instead of silently collapsing policies.
