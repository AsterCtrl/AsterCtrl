# Release checklist

An alpha release requires deterministic locks and generated files, Host tests
under GCC and Clang, sanitizers, `native_sim`/QEMU, both board link builds,
documentation, license inventory, SBOM, checksums and rollback notes. The source
SBOM records discovered dependencies; the release SBOM separately inventories
every downloadable asset with its byte size and SHA-256 digest. Asset inventory
does not replace a dependency SBOM: coverage of fetched yaml-cpp and transitive
dependencies still needs review before release.

The final `v0.2.0` additionally requires dated physical `dev_c` and `mc02`
evidence for console, clock, CAN loopback, UART, SPI and watchdog. USB enumeration
is recorded separately and may remain unverified in the v0.2.0 notes.

This checklist does not publish a release or mutate remote repositories; the
release workflow does that separately. Existing legacy archives remain intact.
The earlier archive-after-release sequence is historical, not an instruction to
rearchive or delete repositories now.

Run `samples/qualification` from `AsterCtrl/asterctrl-boards` on each board,
retain the complete serial log and flashed firmware, and create the evidence
record with `scripts/record_hardware_smoke.py`. Reviewers verify both hashes,
all six pass markers, the board-specific completion marker, the operator and UTC
timestamp. A CI link build or manually written JSON is not accepted as physical
evidence.
