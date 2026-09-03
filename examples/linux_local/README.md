# Single-node Linux

This example places real, typed publisher and subscriber `Module`
implementations in one Linux node. Both use the bounded type generated from
`state.proto`; the route is resolved as `local`, so no transport configuration
is required.

From the repository root:

```sh
uv run aster validate examples/linux_local/{workspace,application,deployment,inventory}.yaml
uv run aster resolve examples/linux_local/workspace.yaml \
  examples/linux_local/deployment.yaml --release \
  --output build/examples/linux-local/deployment.lock.yaml
uv run aster codegen examples/linux_local/workspace.yaml \
  examples/linux_local/deployment.yaml \
  build/examples/linux-local/generated --release
cmake -S examples/linux_local -B build/examples/linux-local/host -G Ninja \
  -DASTERCTRL_SOURCE_DIR="$PWD" \
  -DASTER_GENERATED_DIR="$PWD/build/examples/linux-local/generated/nodes/app"
cmake --build build/examples/linux-local/host
ctest --test-dir build/examples/linux-local/host --output-on-failure
```

The executable initializes and seals the runtime, publishes one bounded
message, verifies one local delivery, and shuts down in lifecycle order.
