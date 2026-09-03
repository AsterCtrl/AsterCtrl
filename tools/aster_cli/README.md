# AsterCtrl CLI

`aster` validates v1alpha2 manifests, compiles Application and Deployment graphs,
emits bounded Linux/Zephyr inputs, and generates fixed-capacity C++ types from
protobuf descriptor sets.

Packages export `.proto` files and declare their bounds/include roots under
`spec.protobuf`. Graph resolution invokes the same bounded-profile analysis as
code generation, then locks the canonical descriptor-and-bounds hash and each
message's maximum encoded size. A Module port may declare `schema_hash` only as
an assertion; release resolution never trusts a hand-entered hash. Connection
`max_size` defaults to the derived maximum and cannot understate it.

The package is managed with `uv`, requires Python 3.12 plus `protoc`, and installs
only the `aster` command. Planning commands are deterministic. `build`, `run`, and
`deploy apply` do not execute external commands unless `--execute` is explicitly
provided.

```shell
uv sync
uv run aster init my_robot
uv run aster doctor --format json
uv run aster validate package.yaml
uv run aster graph workspace.yaml application.yaml --format dot
uv run aster resolve workspace.yaml deployment.yaml --output build/plan.yaml
uv run aster resolve workspace.yaml deployment.yaml --release
uv run aster codegen workspace.yaml deployment.yaml build/generated
uv run aster deploy plan deployment.yaml inventory.yaml build/generated
uv run aster deploy apply deployment.yaml inventory.yaml build/generated --execute
uv run aster deploy status inventory.yaml
uv run aster package lock workspace.yaml --release
```

Code generation creates ``deployment.bundle.yaml`` with a digest for the
Deployment Lock and a SHA-256/size record for every generated file. Deployment
planning verifies the complete bundle. Local apply uses staged, verified release
directories with atomic ``current`` and retained ``previous`` symlinks. SSH uses
argv-only subprocesses and external SSH-agent/config authentication; inventory
and deployment state never contain credentials. Serial and debug-probe flashing
are not deployment Adapters in v0.2.
