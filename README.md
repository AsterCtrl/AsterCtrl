# AsterCtrl

AsterCtrl is an independent robot control framework targeting Linux and Zephyr.
Linux applications load C++ Module Packages from `runtime.yaml`; portable Module
sources are intended to be rebuilt for bounded, static Zephyr deployments.

> **Status:** v0.2 is under active development. The public Interface may still
> change until the first v0.2 release candidate.
> The configuration-driven Linux path is implemented. The v1alpha3 Zephyr
> deployment compiler and transport/plugin integration are still in progress.
> See the [migration status](document/sphinx-en/guides/runtime-v3.md).
> Remaining implementation and verification gaps are tracked in the
> [audit](document/sphinx-en/development/convergence-audit.md).

## Design in one minute

- A Module implements business behaviour once.
- `runtime.yaml` selects Packages, Module instances, executors and instance configuration.
- Modules register Topic and RPC endpoints during `Initialize`; the Runtime
  validates and seals those registrations before `Start`.
- Ordinary Linux has one user-facing `runtime.yaml`; the separate Application graph and
  Deployment Lock model was removed from this path.
- Optional cross-node and Zephyr deployment compilation is being migrated to
  the same runtime configuration, without Module Port-to-Port graphs.
- Every Module lifecycle call uses one canonical C ABI. Linux creates Module
  instances from generated Package factories; Zephyr owns the same handles
  statically.
- `aster codegen` generates the Linux Package factory exports from declarative
  manifests; business Module authors only implement the C++ Interface.

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

Exercise the configuration-driven launcher with the built contract-test Package:

```sh
uv run aster run --runtime build/host-debug/aster_runtime \
  --config build/host-debug/configured-runtime-test.yaml --duration-ms 50
```

Create a minimal Linux Module Package (no custom launcher or graph files):

```sh
uv run aster init hello-aster
uv run cmake -S hello-aster -B hello-aster/build -G Ninja -DASTERCTRL_SOURCE_DIR="$PWD"
cmake --build hello-aster/build
uv run aster run --runtime hello-aster/build/asterctrl/aster_runtime \
  --config hello-aster/build/runtime.yaml
```

`aster_add_package` generates the entry from `package.yaml`. Dependencies belong
to CMake, west and uv; the old `aster package` commands have been removed.
Existing cross-node deployment examples still use v1alpha2 during migration.

The official Zephyr boards live in
[`AsterCtrl/asterctrl-boards`](https://github.com/AsterCtrl/asterctrl-boards).

## Repository layout

```text
src/interface/       C ABI, thin C++ facade and generated Package interface
src/core/            lifecycle and graph-independent runtime Implementation
src/runtime/         Runtime SDK, platform services and internal routing contracts
src/platform/        Linux and Zephyr Adapters
src/transports/      Local, CAN/SocketCAN and USB CDC ACM Adapters
tools/aster_cli/     Python implementation of the `aster` command
cmake/               target setup, dependency pins, tests and SDK export
schemas/             versioned YAML schemas
examples/            retained v1alpha2 deployment regressions (new apps use aster init)
document/            bilingual Sphinx/MyST guides and integrated Doxygen API reference
```

Like AimRT, the source tree has no top-level `include/` directory. Public
headers live beside the implementation under `src/interface`; `cmake --install`
places the same headers under the SDK's conventional `include/` prefix for
downstream consumers. C ABI and C++ facade headers are split by component;
aggregate headers are convenience imports only.

## Project policy

AsterCtrl uses Apache-2.0, Semantic Versioning, GitHub Flow and squash merges.
See [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), and the
[development log](document/sphinx-en/development/index.md).
