# Linux to Zephyr over CAN

The typed IMU publisher and calibration RPC server run on the `dev_c` Zephyr
host; the typed controller subscriber and RPC client run on Linux. Both
endpoints compile against the same bounded Channel and unary RPC Interfaces
generated from `imu.proto`. Route rules select `can0`;
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
and routes the reliable Channel and RPC request/response with bounded retry and
reassembly state. Application Modules use only ordinary Channel and RPC
Interfaces and contain no CAN-specific code. The controller requests
calibration after its first remote IMU sample, so it cannot race the link
handshake during startup.

CI compiles both endpoints with C++20, exceptions and RTTI disabled and tests
Channel and RPC through the lifecycle-managed transport Module with injected
loss/duplicates, plus the Linux Adapter on ``vcan``. This remains a compile and
Host protocol contract, not a claim that a board was flashed or that a physical
CAN loop completed.
