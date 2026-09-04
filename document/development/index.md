# Development log

## 2026-09-04 — v0.2 cutover begins

Decision

: AsterCtrl owns native Linux and Zephyr runtimes. Application and Deployment
  Graphs are explicit, and Linux dynamic packages share a manifest model with
  Zephyr static packages.

Implementation

: Histories from `aster-runtime`, `aster-tools` and `aster-transports`
  were imported. The libxr backend history was retained and its Implementation
  was removed in a dedicated migration commit.

Verification

: Legacy test and firmware evidence was frozen before changing the active
  implementation. New milestone gates are recorded in CI and release notes.

Known limitations

: UDP, complete SIL/PIL, runtime topology discovery, ROS and AimRT bridges are
  deferred. USB hardware enumeration is not a v0.2 release gate.

New entries use {doc}`template` and must distinguish executed evidence from a
planned gate.

```{toctree}
:hidden: true

2026-09-04-v0.2-foundation
template
```
