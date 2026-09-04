Transport Adapters
==================

The Graph contains logical Channel and RPC routes. Transport details remain in
the Deployment and never enter Module Interfaces.

Local
  Bounded in-process dispatch for Channel and RPC. Registration is sealed before
  use and delivery records capacity and callback failures.

CAN and SocketCAN
  Stable Route IDs, compact frames, fragmentation/reassembly, reliable RPC
  acknowledgement and retry, deadline handling, peer restart detection,
  backpressure and statistics. Runtime handshakes validate the expected
  Deployment ID, node identity and Schema Hash; they do not discover new
  topology.
  v0.2 uses classic 8-byte CAN frames. Application Route IDs 1--7 are reserved
  for link control, so automatic IDs begin at 8 and a CAN Deployment accepts
  IDs only through 511. The bounded wire profile permits at most 16 fragments:
  94 message bytes for a Channel after its timestamp header, or 84 bytes for an
  RPC after its request header. ``aster resolve`` enforces these limits and
  generates a transmit FIFO large enough for one maximum-size message.
  Link utilization uses the resolved fragment count and a conservative 160-bit
  budget per classic CAN frame, including framing, acknowledgement and stuffing
  allowance.
  Each endpoint uses the same logical Deployment resource name. Its Zephyr
  Hardware Profile maps to a Devicetree node label, while its Linux profile maps
  to a ``socketcan`` interface such as ``can0`` or ``vcan0``.
  On Zephyr, ``CanDeviceAdapter`` owns controller start/stop and one standard-ID
  RX filter. Its driver callback only copies a frame and timestamp into a
  bounded ``k_msgq``; ``Poll`` performs protocol dispatch later in executor
  thread context. A second bounded FIFO admits transmit bursts, while the
  Adapter gives the controller only one frame at a time to preserve equal-ID
  fragment order across multi-mailbox drivers. Mailbox backpressure leaves the
  FIFO head queued for the next thread-context ``Send`` or ``Poll`` call.
  Completion, rejection, failure, abort and RX overflow counts remain visible
  through fixed-size statistics. ``Send`` and ``Poll`` reject both declared and
  actual ISR callers. Callers must obtain a successful ``Stop`` before the
  Adapter's storage or receiver state leaves scope; a failed controller stop is
  exposed as ``kStopFailed`` and retains callback state for a retry.
  Admission is atomic only against an initially empty FIFO; a busy link may
  reject a later fragment with bounded backpressure. CAN FD remains a future
  Transport implementation and is rejected by the v0.2 resolver.

USB CDC ACM
  The same COBS and CRC32C framing runs over a Zephyr CDC ACM byte stream and a
  Linux TTY. A Deployment must specify an explicit product VID and PID. Values
  in examples are development-only and must not be shipped as an allocation.
  The Zephyr endpoint maps to a CDC ACM Devicetree node and the Linux endpoint
  maps to an absolute ``/dev`` path with the ``tty`` backend.

UDP, Zenoh, ROS 2, AimRT, gRPC and MQTT are not official v0.2 transports. They
may be supplied by versioned transport plugins, but the resolver rejects a
non-official type without a declared package and backend.
