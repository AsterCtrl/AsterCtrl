# Debugging

## New Linux path

Use aster doctor, then choose the depth of checking:

```sh
aster validate runtime.yaml
aster run --config runtime.yaml --check
aster run --config runtime.yaml
```

validate reuses the native parser and checks configuration without loading
Packages or running Modules. run --check loads, initializes, seals and shuts down:
application Initialize/Shutdown behavior really executes. It checks local
registrations, not arbitrary C++ statically or cross-node handshakes.
Both commands accept --runtime PATH when the executable is not on PATH.

Logs include instance identity and execution context. Embedding applications
can inspect NodeRuntime's state, diagnostic and VisitGraph. The latter lists
Module instances only, not the complete communication graph or distributed
monitoring. A complete health-monitoring endpoint is not implemented.

Reproduce memory/concurrency issues with Host ASan/UBSan/TSan. vcan requires
Linux. Pseudo-TTY tests exercise USB framing, not physical enumeration or CAN
electrical connectivity.

## Legacy deployment and Zephyr regressions

The old graph/resolve tools and deployment.lock.yaml belong only to the retained
v1alpha2 path; they are not prerequisites for new Linux debugging.
Static configuration, overlays, image sizes, ISR handoff and board smoke still
need verification on the new Zephyr path.

Reports should include version, sanitized runtime.yaml, platform, reproduction
commands and the first error. Include a legacy Lock only for legacy deployment
issues, and firmware/log hashes for hardware issues.
