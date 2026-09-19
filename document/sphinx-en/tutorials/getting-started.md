# Getting started

Install Python 3.12, uv, CMake 3.28+, Ninja and `protoc`, then run:

```console
uv sync --all-groups
uv run aster doctor
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Create, build and run a minimal Linux Module Package:

```console
uv run aster init hello-aster
uv run cmake -S hello-aster -B hello-aster/build -G Ninja \
  -DASTERCTRL_SOURCE_DIR="$PWD"
cmake --build hello-aster/build
uv run aster run --runtime hello-aster/build/asterctrl/aster_runtime \
  --config hello-aster/build/runtime.yaml
```

The generated YAML and Module source are ordinary project inputs and may be
committed. `ASTERCTRL_SOURCE_DIR` is convenient while developing against a
checkout; installed SDKs are found with `find_package(AsterCtrl)` instead.

The template has a Module header/source, Package export metadata, CMake and
runtime.yaml. The current model has no custom main, separate Application graph or
Deployment Lock file.
`aster_add_package(demo MANIFEST package.yaml)` generates the C ABI entry in
the build directory and links only Module/Package Interfaces. Edit runtime.yaml
and rerun CMake (or build, which reruns configuration) to update the launch
configuration without recompiling the Module. Stop with Ctrl-C.

For an installed SDK, replace ASTERCTRL_SOURCE_DIR with CMAKE_PREFIX_PATH and
put its bin directory on PATH. The helper needs the Python 3.12 aster_cli
environment active during CMake configuration. See [migration status](../guides/runtime-v3.md)
for the remaining Zephyr and cross-node work.
