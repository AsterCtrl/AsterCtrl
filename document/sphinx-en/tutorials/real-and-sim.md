# Algorithms, hardware and simulation

Simulation engines do not belong inside the AsterCtrl core. Algorithms should
consume messages and RPCs; Driver Modules obtain or actuate real-world data,
while Bridge Modules communicate with external simulators. Change the supplying
Modules and communication configuration, not platform/simulation branches in
algorithm source.

Linux already selects instances, parameters, namespace/remap and executors in
runtime.yaml and supports mixed static/dynamic Modules. `application.yaml` was removed
from the current user model; a generic Hardware Profile is not required. Source reuse still depends on
compatible message semantics, clock behavior and capacity contracts; changing a
filename alone does not establish equivalence.

**ROS Bridge, Linux IP Transport, MuJoCo/Isaac integration, configured Clock
replacement, record/replay and PIL are not delivered.** They are not executable
tutorials in this iteration.

examples/provider_swap is only a legacy v1alpha2 Provider/Clock substitution
regression, not the new user model. The complete v1alpha3 same-algorithm
Linux/Zephyr native_sim example also remains an acceptance item.
