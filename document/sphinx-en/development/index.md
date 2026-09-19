# Development log

Current progress is tracked in the [implementation audit](convergence-audit.md).
The explicit-graph decision below was superseded by configuration-driven
convergence; it is retained as history, not current user guidance.

## 2026-09-04 — v0.2 cutover begins (historical)

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

convergence-audit
2026-09-05-core-convergence
2026-09-04-v0.2-foundation
aimrt-documentation-gap-analysis
template
```
