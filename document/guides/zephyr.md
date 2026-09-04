# Zephyr Runtime

The manifest pins Zephyr 4.4.0 and the build workflow installs Zephyr SDK
1.0.1. `aster codegen` emits, per node, a Kconfig fragment, Devicetree overlay,
CMake input, static composition and resolved resource/route tables. Application
authors edit YAML; only Runtime, board and driver authors maintain Kconfig or
base Devicetree files directly.

The generated overlay creates `aster-<resource>` aliases that point at the
validated node labels and marks those devices `okay`. The generated Kconfig
fragment enables the Zephyr CAN subsystem and Aster CAN Adapter when a CAN
resource is present. For USB CDC it selects the new Zephyr USB device stack,
CDC ACM class and Aster adapter, and records the
validated product VID/PID as `CONFIG_ASTERCTRL_USB_VID` and
`CONFIG_ASTERCTRL_USB_PID`. Those identifiers remain product metadata; the
new USB stack consumes them when the application defines its USBD context.

Generated Module composition is statically linked. C++ exceptions and RTTI are
disabled. Thread executors use bounded `k_msgq` queues, ISR work crosses into
thread context through bounded handoff, and the allocator uses fixed storage.
Generated capacities include every Module port placed on the node, including
optional ports without an Application route. Subscriber capacity is independent
from Module capacity. `deployment.lock.yaml` records the exact Kconfig inputs
and a conservative fixed Runtime RAM bound, so invalid graphs fail during
`aster resolve` rather than during Kconfig or startup.

The alpha build matrix executes the shared Runtime contract on `native_sim`
and QEMU and compile-links `dev_c/stm32f407xx` and `mc02/stm32h723xx`.
Compile success is not a substitute for the console, clock, CAN loopback, UART,
SPI and watchdog tests required on both physical boards before v0.2.0.

The boards repository provides `samples/qualification` for those six checks.
Its console output has machine-readable `ASTERCTRL_HW_SMOKE` markers, and
`scripts/record_hardware_smoke.py` binds a passing log to the board name,
operator, UTC time, repository revision and exact firmware/log SHA-256 values.
The recorder deliberately refuses incomplete logs and existing output files.
The UART marker proves direct console TX; connector RX and an external CAN
transceiver still need a wired fixture when those signal paths are in scope.

Board definitions are maintained in the separate public
`AsterCtrl/asterctrl-boards` repository. Their public names intentionally have
no institutional prefix.
