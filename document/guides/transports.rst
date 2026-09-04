Transport Adapters
==================

The Graph contains logical Channel and RPC routes. Transport details remain in
the Deployment and never enter Module Interfaces.

``ChannelPacketEgress`` and ``ChannelPacketIngress`` form the shared seam
between the local Channel registry and packet-oriented Transports. They carry
the resolved Route ID, Schema Hash, sequence and timestamps without knowing
whether the Adapter below is USB, an in-memory test link, or a future reliable
CAN packet Adapter. An optional maximum age becomes an absolute wire deadline;
leave it disabled unless both nodes use the same synchronized clock domain.
``ChannelTransportModule`` owns route registration, Transport start/stop and
bounded executor-driven polling. As generated infrastructure it starts before
Application Modules, stops after them, and therefore keeps link lifecycle out
of business code.

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
  The generator constructs a ``CanChannelTransportModule`` as infrastructure,
  ahead of Application Modules. It binds resolved Channel routes to either the
  best-effort bridge or the reliable sender/receiver, owns Adapter lifecycle,
  and polls handshake, heartbeat, time synchronization, retries and
  reassembly from the bounded executor queue. Application frames remain gated
  until the peer's Deployment and Schema hashes match and the clock is
  synchronized. v0.2 therefore requires exactly one synchronized authority and
  supports one peer per CAN node; the resolver rejects other layouts before
  code generation. The generated fixed storage and one polling slot per
  external Transport are included in the Runtime resource budget.
  On Zephyr, ``CanDeviceAdapter`` owns controller start/stop and one standard-ID
  RX filter. Its driver callback only copies a frame and timestamp into a
  bounded ``k_msgq``; ``Poll`` performs protocol dispatch later in executor
  thread context. A second bounded FIFO admits transmit bursts, while the
  Adapter gives the controller only one frame at a time to preserve equal-ID
  fragment order across multi-mailbox drivers. Mailbox backpressure leaves the
  FIFO head queued for the next thread-context ``Send`` or ``Poll`` call.
  Completion, rejection, failure, abort and RX overflow counts remain visible
  through fixed-size statistics. ``Send`` and ``Poll`` reject both declared and
  actual ISR callers. Each Adapter receives its own process-lifetime
  ``CanCallbackFence``. A successful ``Stop`` detaches the Adapter before its
  storage or receiver state may leave scope, so a driver's late completion can
  touch only the fence; a failed controller stop is exposed as ``kStopFailed``
  and retains callback state for a retry. A fence is one-shot bound and cannot
  be reused by a different Adapter object. For the same reason, a successfully
  stopped Adapter is retired; create a new Adapter and fence instead of
  restarting it.
  Admission is atomic only against an initially empty FIFO; a busy link may
  reject a later fragment with bounded backpressure. CAN FD remains a future
  Transport implementation and is rejected by the v0.2 resolver.
  CAN RPC wire primitives remain independently tested, but generated CAN RPC
  route composition is still release work; ``kRequiresTransportWiring`` keeps
  such a Node from starting silently.

USB CDC ACM
  The same COBS and CRC32C framing runs over a Zephyr CDC ACM byte stream and a
  Linux TTY. A Deployment must specify an explicit product VID and PID. Values
  in examples are development-only and must not be shipped as an allocation.
  Zephyr Channel routes are emitted as lifecycle-managed infrastructure and use
  ``options.poll_interval_us`` (default 1000, bounded to 100--1000000) for
  executor-driven receive polling. Their fixed framing and bridge storage is
  included in the Deployment Lock's RAM budget.
  The Zephyr endpoint maps to a CDC ACM Devicetree node. The Linux endpoint maps
  to an absolute TTY path, opens it at ``options.baud_rate`` (default 115200),
  and installs the same generated Channel bridge before Runtime startup.
  Supported baud rates are 9600, 57600, 115200, 230400, 460800 and 921600;
  platform support for the three highest rates still depends on ``termios``.

UDP, Zenoh, ROS 2, AimRT, gRPC and MQTT are not official v0.2 transports. They
may be supplied by versioned transport plugins, but the resolver rejects a
non-official type without a declared package and backend.
