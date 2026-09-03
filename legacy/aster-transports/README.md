# Aster Transports

Transport backends for the framework Channel and internal RPC primitives.

The first production backend is the compact, deployment-compiled classic CAN
data plane. Application modules do not depend on this repository directly.

## aster-can v1

- The 11-bit arbitration ID contains a 2-bit priority and a 9-bit static Route
  ID. Application Route IDs are `8..511`; `1..7` remain control-plane IDs.
- Topic traffic uses a latest-only Fast Path. A frame carries a 6-bit sequence;
  fragmented samples carry up to 6 data bytes per frame and are superseded by a
  newer sample.
- Service, Action, handshake and configuration traffic use a Reliable Path with
  whole-message retry, ACK and duplicate suppression.
- Both paths are bounded to 16 classic-CAN fragments (96 bytes before frame
  headers). Deployment compilation rejects a larger wire payload.
- Topic envelopes carry a 16-bit millisecond source timestamp. The deployment
  compiler includes that metadata, Reliable ACKs and worst-case CAN bit
  stuffing in its link budget.

All codecs and bridges use fixed-capacity storage. The host tests exercise
loss, retry, replay rejection, recovery and allocation-free hot paths.
