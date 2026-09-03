# Fake and hardware Provider swap

`application.yaml` owns one stable `example.Clock/v1` Requirement/Provider
binding. The two workspaces resolve the logical `clock/clock-provider` module to
either `packages/fake_clock` or `packages/hardware_clock`; the Application and
`ClockClient` source do not change. Both implementations consume the same
`CoreRef::clock()` seam. `deployment.sim.yaml` targets local Linux, while
`deployment.real.yaml` targets the qualified `dev_c/stm32f407xx` Zephyr board.
The hardware implementation declares a `gpio` capability; resolution binds it
uniquely to the `rtc_irq` Devicetree resource and records that binding in the
Deployment Lock.

```sh
uv run aster resolve examples/provider_swap/workspace.sim.yaml \
  examples/provider_swap/deployment.sim.yaml --release
uv run aster resolve examples/provider_swap/workspace.real.yaml \
  examples/provider_swap/deployment.real.yaml --release
uv run aster codegen examples/provider_swap/workspace.sim.yaml \
  examples/provider_swap/deployment.sim.yaml \
  build/examples/provider-swap/generated --release
cmake -S examples/provider_swap -B build/examples/provider-swap/host -G Ninja \
  -DASTERCTRL_SOURCE_DIR="$PWD" \
  -DASTER_GENERATED_DIR="$PWD/build/examples/provider-swap/generated/nodes/simulation"
cmake --build build/examples/provider-swap/host
ctest --test-dir build/examples/provider-swap/host --output-on-failure
```

The Linux executable injects `ManualClock` and runs the resolved fake Provider.
The real composition is compile-checked, but exercising its hardware clock
requires a flashed `dev_c` board.
