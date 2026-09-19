# AsterCtrl v0.2.0-alpha.1

Draft release template: core convergence is in progress. This file is not evidence
of a published or fully accepted release. The configuration-driven Linux Runtime,
minimal project template and Package entry generator use v1alpha3 and ABI 2.
Old Packages require rebuilding; old Deployment inputs are not silently migrated.

The repository still contains v1alpha2 Zephyr/native_sim/QEMU and cross-node
regression paths. They do not validate the pending v1alpha3 static deployment
compiler. CorePlugin production lifecycle, new-runtime CAN/USB integration and
communication-contract identity remain incomplete. A compiled image is not
evidence of a working data link or dual-platform acceptance.

Known hardware-validation gaps:

- `dev_c` and `mc02` compile jobs remain configured in CI, but the new migration
  has not been verified on both boards. Console, clock, CAN, UART, SPI and watchdog
  physical-board smoke is not certified.
- The existing CAN Device Adapter and ISR handoff have native_sim loopback tests;
  rerunning them with the new Interface and completing the same-source
  Linux-to-Zephyr data path remain acceptance work.
- USB CDC ACM framing is tested on the host, but real USB enumeration is not a
  release gate and remains unverified.
- This prerelease is not `v0.2.0`; legacy repositories must not be archived on
  the strength of this alpha alone.

Every packaged artifact and the SBOM is listed in `SHA256SUMS`. The release
metadata bundle contains schemas, deterministic deployment locks, the
changelog, development log, and rollback procedure.
