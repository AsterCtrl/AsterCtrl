# Linux to Zephyr over CAN

The typed IMU publisher runs on the `dev_c` Zephyr host and the typed controller
subscriber runs on Linux. Both endpoints compile against the same bounded
`ImuState` Interface generated from `imu.proto`. A route rule selects `can0`;
the resolver checks host reachability, the Zephyr Devicetree and Linux
SocketCAN resources, MTU
fragmentation, bitrate utilization, time mapping, and host stack budgets.

```sh
uv run aster resolve examples/linux_zephyr_can/workspace.yaml \
  examples/linux_zephyr_can/deployment.yaml --release
uv run aster codegen examples/linux_zephyr_can/workspace.yaml \
  examples/linux_zephyr_can/deployment.yaml build/generated/can --release
uv run aster deploy plan examples/linux_zephyr_can/deployment.yaml examples/linux_zephyr_can/inventory.yaml build/generated/can
```

Deployment code generation emits the bounded header and both Node compositions
in one deterministic tree. CI compiles them with C++20, exceptions and RTTI
disabled. This is a compile contract, not a claim that a board was flashed or
that a physical CAN loop completed.
