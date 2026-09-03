Switching real and simulation environments
==========================================

Keep ``application.yaml`` and Module source unchanged. Build
``deployment.sim.yaml`` to select fake Hardware and Clock Adapters, or
``deployment.real.yaml`` to select Linux/Zephyr device Adapters.

This release demonstrates Adapter substitution but does not include a physics
engine, scenario system, record/replay system or PIL harness.
