Testing policy
==============

Tests are arranged by contract rather than by operating system alone:

* Runtime tests cover registration sealing, lifecycle rollback, Channel, RPC,
  Parameter, CoreRef and the C ABI.
* CLI fixtures cover every Graph validation rule and byte-for-byte deterministic
  locks and generated inputs.
* Protobuf tests cover official-runtime golden vectors, unknown fields,
  truncation, illegal wire types, bounds and malformed input. Every pull request
  also runs the generated decoder under LLVM libFuzzer with ASan and UBSan.
* Transport tests cover Local dispatch, CAN fragmentation/loss/reorder/restart,
  SocketCAN ``vcan`` and USB COBS/CRC framing through a pseudo-TTY.
* Zephyr tests execute the portable pub/sub Module source in ``native_sim`` and
  QEMU. ``native_sim`` also drives the CAN Device Adapter through Zephyr's
  loopback controller, including its RX handoff from the driver callback into
  thread context, an equal-ID transmit burst larger than three controller
  mailboxes, immediate-stop accounting and real ``irq_offload`` rejection.
  Both official boards are compile-linked with size regression limits.

Physical-board smoke results are release evidence, not ordinary CI simulation.
The v0.2.0 release checklist requires console, clock, CAN loopback, UART, SPI and
watchdog evidence for both boards. USB enumeration is tracked separately as an
explicitly incomplete hardware verification item for v0.2.0.
