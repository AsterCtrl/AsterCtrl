# CLI reference

The only public executable is `aster` and the Python import package is
`aster_cli`.

`aster init`

: Create a minimal Linux Module Package and runtime.yaml, with no launcher or Port graph.

`aster doctor`

: Report versions and availability of Python, uv, CMake, Ninja, protoc, west and
  the selected compilers.

The old `aster package` dependency-management commands are removed. Use CMake,
west and uv; the Package Manifest only describes framework exports.

`aster validate runtime.yaml [--runtime PATH]`

: Use the native Runtime parser without loading Packages or running Modules.
  Requires a built `aster_runtime` on PATH or at the supplied path. Other document
  kinds use their schema validator. This does not check C++ registrations.

`aster graph` / `aster resolve` / `aster build`

: Legacy v1alpha2 graph compilation, node generation and CMake/west build plans.
  These commands print a migration warning. They do not consume v1alpha3 runtime
  configurations and `graph` is not a live communication-graph query.

`aster run --config runtime.yaml`

: Start the generic Host Runtime directly, without a Deployment Lock or `--execute`.
  The CLI finds `aster_runtime` on PATH, or accepts `--runtime PATH`.
  `--check` initializes and seals registrations without starting Modules;
  `--duration-ms N` performs a bounded smoke run.

`init`, Package entry generation and the new Runtime use v1alpha3. Deployment
compilation still uses v1alpha2 during migration. See the [migration guide](../guides/runtime-v3.md).

`aster codegen --package package.yaml --output build/package`

: Generate the Linux C ABI entry and CMake source list. Normally called by
  `aster_add_package`; no Package-owned Python is executed.

`aster deploy plan|apply|status`

: Verify a versioned Deployment Bundle, stage and atomically activate it through
  Local or SSH Inventory Adapters, and report its current Deployment ID and
  digest. SSH process execution and all mutations require `--execute`.
  This retained v1alpha2 tool switches files; it does not start systemd services
  or flash MCU firmware. See [deployment](../guides/deployment.md).

`aster codegen --proto ...` / `aster codegen --descriptor ...`

: Compile the bounded Protobuf profile from source files or a descriptor set.

Use `aster <command> --help` for paths and command-specific options.
