# Testing policy

Tests and required gates are arranged by contract rather than by operating
system alone. This page describes coverage and acceptance policy, not proof
that the current revision passed each environment. Executed evidence belongs
in the [implementation audit](../development/convergence-audit.md).

- Runtime tests cover registration sealing, lifecycle rollback, Channel, RPC,
  Parameter, CoreRef and the C ABI.
- CLI fixtures cover runtime validation, the generated Package/installed SDK
  workflow, legacy Graph validation rules and byte-for-byte deterministic locks.
- Protobuf tests cover official-runtime golden vectors, unknown fields,
  truncation, illegal wire types, bounds and malformed input. Every pull request
  also runs the generated decoder under LLVM libFuzzer with ASan and UBSan.
- Transport regression tests cover Local dispatch, retained CAN fragmentation/loss/reorder/restart,
  reliable Channel acknowledgement/retry, generated CAN RPC routing and stale
  completion suppression after peer restart, SocketCAN Adapter lifecycle on
  `vcan` and USB COBS/CRC framing through a pseudo-TTY. The example
  gate also starts the generated Linux USB node on a temporary pseudo-TTY,
  observes a complete outbound frame, delivers `SIGTERM` and requires a clean
  Runtime shutdown.
- The Zephyr workflow attempts to execute the portable pub/sub Module source in `native_sim` and
  QEMU. `native_sim` also drives the CAN Device Adapter through Zephyr's
  loopback controller, including its RX handoff from the driver callback into
  thread context, an equal-ID transmit burst larger than three controller
  mailboxes, immediate-stop accounting and real `irq_offload` rejection.
  Both official boards must compile-link within size regression limits. These
  consult the corresponding GitHub Actions run for current evidence. The new
  v1alpha3 static-deployment path is not implemented yet.

Physical-board smoke results are release evidence, not ordinary CI simulation.
The v0.2.0 release checklist requires console, clock, CAN loopback, UART, SPI and
watchdog evidence for both boards. USB enumeration is tracked separately as an
explicitly incomplete hardware verification item for v0.2.0.
