# AsterCtrl v0.2 alpha

This is a compile-only prerelease of the native Linux and Zephyr architecture.
It is intended for API review, deterministic graph/code-generation testing, and
early board build validation.

Known hardware-validation gaps:

- `dev_c` and `mc02` firmware is compiled in CI but the console, clock, CAN,
  UART, SPI, and watchdog physical-board smoke suite is not yet certified.
- USB CDC ACM framing is tested on the host, but real USB enumeration is not a
  release gate and remains unverified.
- This prerelease is not `v0.2.0`; legacy repositories must not be archived on
  the strength of this alpha alone.

Each downloadable artifact is covered by `SHA256SUMS`. The release metadata
bundle contains schemas, deterministic deployment locks, the changelog,
development log, and rollback procedure.
