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
  Each endpoint uses the same logical Deployment resource name. Its Zephyr
  Hardware Profile maps to a Devicetree node label, while its Linux profile maps
  to a ``socketcan`` interface such as ``can0`` or ``vcan0``.

USB CDC ACM
  The same COBS and CRC32C framing runs over a Zephyr CDC ACM byte stream and a
  Linux TTY. A Deployment must specify an explicit product VID and PID. Values
  in examples are development-only and must not be shipped as an allocation.
  The Zephyr endpoint maps to a CDC ACM Devicetree node and the Linux endpoint
  maps to an absolute ``/dev`` path with the ``tty`` backend.

UDP, Zenoh, ROS 2, AimRT, gRPC and MQTT are not official v0.2 transports. They
may be supplied by versioned transport plugins, but the resolver rejects a
non-official type without a declared package and backend.
