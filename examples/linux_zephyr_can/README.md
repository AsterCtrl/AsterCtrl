# Linux to Zephyr over CAN

The typed IMU publisher runs on the `dev_c` Zephyr host and the typed controller
subscriber runs on Linux. Both endpoints compile against the same bounded
`ImuState` Interface generated from `imu.proto`. A route rule selects `can0`;
the resolver checks host reachability, the Zephyr Devicetree and Linux
SocketCAN resources, MTU fragmentation, bitrate utilization, synchronized time,
and host stack/RAM budgets.

```sh
uv run aster resolve examples/linux_zephyr_can/workspace.yaml \
  examples/linux_zephyr_can/deployment.yaml --release
uv run aster codegen examples/linux_zephyr_can/workspace.yaml \
  examples/linux_zephyr_can/deployment.yaml build/generated/can --release
uv run aster deploy plan examples/linux_zephyr_can/deployment.yaml examples/linux_zephyr_can/inventory.yaml build/generated/can
```

Deployment code generation emits the bounded header and both Node compositions
in one deterministic tree. The generated infrastructure owns the Zephyr CAN or
Linux SocketCAN Adapter, performs Deployment/Schema handshake and time sync,
and routes the reliable Channel with bounded retry and reassembly state. The
IMU Module publishes periodically only through the ordinary Channel Interface;
it contains no CAN-specific code.

CI compiles both endpoints with C++20, exceptions and RTTI disabled and tests
the transport Module with injected loss/duplicates plus the Linux Adapter on
``vcan``. This remains a compile and Host protocol contract, not a claim that a
board was flashed or that a physical CAN loop completed.
