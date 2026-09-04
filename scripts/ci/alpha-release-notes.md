# AsterCtrl v0.2.0-alpha.1

This is a compile-only prerelease of the native Linux and Zephyr architecture.
It is intended for API review, deterministic graph/code-generation testing, and
early board build validation.

The Zephyr Runtime owner is exercised end to end for a local graph in
`native_sim` and QEMU. Cross-node CAN and USB images are intentionally different:
they prove deterministic composition, Kconfig, Devicetree and link integration,
but refuse to start the application Runtime until generated Transport route
wiring is present. A compiled cross-node image is therefore not evidence of a
working data link.

Known hardware-validation gaps:

- `dev_c` and `mc02` firmware is compiled in CI but the console, clock, CAN,
  UART, SPI, and watchdog physical-board smoke suite is not yet certified.
- The Zephyr CAN Device Adapter and bounded ISR-to-thread handoff are covered by
  `native_sim` loopback. Automatic CAN/USB route-bridge construction and the
  Linux-to-Zephyr end-to-end data path remain incomplete in this alpha.
- USB CDC ACM framing is tested on the host, but real USB enumeration is not a
  release gate and remains unverified.
- This prerelease is not `v0.2.0`; legacy repositories must not be archived on
  the strength of this alpha alone.

Every packaged artifact and the SBOM is listed in `SHA256SUMS`. The release
metadata bundle contains schemas, deterministic deployment locks, the
changelog, development log, and rollback procedure.
