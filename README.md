# AsterCtrl

AsterCtrl is a deterministic control framework that runs natively on Linux and
Zephyr. It keeps control Modules independent from operating systems, hardware,
and transports by resolving an Application Graph against an environment-specific
Deployment Graph before the build starts.

> **Status:** v0.2 is under active development. The public Interface may still
> change until the first v0.2 release candidate.

## Design in one minute

- A Module implements business behaviour once.
- An Application declares Module Instances and typed Channel/RPC connections.
- A Deployment places those instances and selects Clock, Hardware and Transport
  Adapters.
- `aster resolve` validates the complete graph and emits deterministic build
  inputs for CMake or west.
- Linux loads versioned Module Bundles and Core Plugins through a narrow C ABI;
  Zephyr statically links the same package manifests.

AimRT inspired parts of the lifecycle and configuration model, but AsterCtrl
does not require AimRT. The core and default builds also contain no ROS, XRobot,
libxr or FreeRTOS runtime dependency.

## Quick start

Requirements: Python 3.12, [uv](https://docs.astral.sh/uv/), CMake 3.28+,
Ninja and ``protoc``. Zephyr builds additionally require west and the pinned
Zephyr SDK described in the documentation.

```sh
uv sync --all-groups
uv run aster doctor
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Create and inspect a project:

```sh
uv run aster init hello-aster
uv run aster validate hello-aster/{workspace,application,deployment.sim,inventory.sim}.yaml
uv run aster graph hello-aster/workspace.yaml hello-aster/application.yaml
uv run aster resolve hello-aster/workspace.yaml hello-aster/deployment.sim.yaml
uv run aster codegen hello-aster/workspace.yaml hello-aster/deployment.sim.yaml \
  hello-aster/build/generated
cmake -S hello-aster -B hello-aster/build/host -G Ninja \
  -DASTERCTRL_SOURCE_DIR="$PWD" \
  -DASTER_GENERATED_DIR="$PWD/hello-aster/build/generated/nodes/app"
cmake --build hello-aster/build/host
hello-aster/build/host/aster_app
```

The official Zephyr boards live in
[`AsterCtrl/asterctrl-boards`](https://github.com/AsterCtrl/asterctrl-boards).

## Repository layout

```text
include/aster/       portable public Interfaces and the C ABI
src/core/            lifecycle and graph-independent runtime Implementation
src/platform/        Linux and Zephyr Adapters
src/transports/      Local, CAN/SocketCAN and USB CDC ACM Adapters
tools/aster_cli/     Python implementation of the `aster` command
schemas/             versioned YAML schemas
examples/            executable Application/Deployment examples
document/            Sphinx/MyST Markdown concepts, tutorials, references and dev logs
```

## Project policy

AsterCtrl uses Apache-2.0, Semantic Versioning, GitHub Flow and squash merges.
See [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), and the
[development log](document/development/index.md).
