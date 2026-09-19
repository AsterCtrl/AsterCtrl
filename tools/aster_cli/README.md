# AsterCtrl CLI

The Python 3.12 package `aster_cli` installs the single public command `aster`.
Use uv for the Python environment, CMake for Host dependencies, and west for
Zephyr modules. The former `aster package` dependency manager is removed.

## Configuration-driven Linux

`aster init DIRECTORY` creates a minimal Module header/source, v1alpha3
package.yaml, runtime.yaml and CMakeLists.txt. There is no custom launcher,
Application graph or Deployment Lock.

`aster_add_package(target MANIFEST package.yaml)` invokes
`aster codegen --package package.yaml --output BUILD_DIRECTORY` during CMake
configuration. It generates the C ABI entry and links only Module/Package
Interfaces. The manifest does not execute repository-owned Python.

`aster run --config runtime.yaml` starts the generic Runtime immediately; there
is no --execute flag. Use --runtime PATH when aster_runtime is not on PATH,
--check to initialize/seal without starting, or --duration-ms N for a smoke run.

`aster validate runtime.yaml [--runtime PATH]` uses that same native parser
without loading Packages or running Modules. It does not verify C++ registrations.

## Bounded messages and migration

`aster codegen --proto ...` or `--descriptor ...` generates fixed-capacity
C++ types with the existing bounded Protobuf profile. This needs protoc when
compiling .proto sources; ordinary Package generation does not.

Deployment graph compilation, cross-node build inputs and bundle installation
still consume v1alpha2 during the v1alpha3 migration. They are regression paths,
not a second long-term configuration model. Existing deploy plan/apply/status
retain digest verification, staged installation and rollback. Build and deployment
mutation still require --execute.
Old graph/resolve/build commands print migration warnings. Deployment activation
switches files; it does not start systemd services or flash an MCU.

See the bilingual getting-started and runtime-v3 guides in document/ for
verified commands and the remaining Zephyr/transport/plugin work.
