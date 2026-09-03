Debugging
=========

Start with ``aster doctor`` and ``aster validate``. Resolver errors name both
endpoints or resources and occur before a compiler is invoked. ``aster graph``
can emit JSON for tooling or DOT for visual inspection; ``aster resolve`` emits
the immutable placement, route, budget and provenance record.

At runtime, inspect the Supervisor state and first ``RuntimeFailure``. Stable
Status categories distinguish configuration, resource, timeout, protocol,
lifecycle and platform failures. Transport statistics expose dropped,
duplicated, retried, timed-out and backpressured packets without leaking those
details into Module code.

For memory or concurrency failures, reproduce on the Host with ``host-asan`` or
``host-tsan`` before moving to a board. SocketCAN tests use a Linux ``vcan``
interface; USB framing tests use a pseudo-TTY. Zephyr ``native_sim`` gives the
same portable Module contract a debuggable process before QEMU or physical
hardware is involved.
