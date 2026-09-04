# CLI reference

The only public executable is `aster` and the Python import package is
`aster_cli`.

`aster init`

: Create a minimal, validated Application and default single-node Deployment.

`aster doctor`

: Report versions and availability of Python, uv, CMake, Ninja, protoc, west and
  the selected compilers.

`aster package add|remove|list|lock`

: Manage declarative package inputs and produce a deterministic package lock.

`aster validate` / `aster graph` / `aster resolve`

: Validate individual documents, compile the logical Application Graph and
  resolve it against a Deployment.

`aster codegen` / `aster build` / `aster run`

: Emit deterministic node inputs plus Package-exported bounded Protobuf types,
  plan or execute CMake/west, and plan or launch a Linux artifact. Mutation and
  process execution require explicit flags.

`aster deploy plan|apply|status`

: Verify a versioned Deployment Bundle, stage and atomically activate it through
  Local or SSH Inventory Adapters, and report its current Deployment ID and
  digest. SSH process execution and all mutations require `--execute`.

`aster codegen --proto ...` / `aster codegen --descriptor ...`

: Compile the bounded Protobuf profile from source files or a descriptor set.

Use `aster <command> --help` for paths and command-specific options.
