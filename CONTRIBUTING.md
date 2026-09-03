# Contributing

## Workflow

1. Open an issue for architectural changes or public Interface changes.
2. Branch from `main` and keep the change focused.
3. Add tests and user-facing documentation with the Implementation.
4. Run the checks below and open a pull request.
5. Maintainers squash-merge after required checks and review pass.

Commit subjects use `scope: lowercase summary`, for example
`runtime: seal registrations before start`. Releases follow Semantic Versioning.

## Local checks

```sh
uv sync --all-groups
uv run --package aster-cli pytest tests/cli
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Portable code must not include Zephyr, POSIX, ROS, AimRT, XRobot, libxr or STM32
HAL headers. Put platform behaviour behind an existing Interface and add an
Adapter only when behaviour actually varies.

Public Interface changes require a compatibility note in `CHANGELOG.md` and a
contract test. Do not add unbounded allocation to Zephyr hot paths.
