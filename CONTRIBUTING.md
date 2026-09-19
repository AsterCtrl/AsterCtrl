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
uv run --package aster-cli python -m pytest tests/cli
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
cmake -E make_directory build/document/doxygen
doxygen document/doxygen/Doxyfile
uv run python -m sphinx -W --keep-going -b html \
  document/sphinx-en build/document/html
uv run python -m sphinx -W --keep-going -b html \
  document/sphinx-cn build/document/html/zh_CN
```

Technical documentation under `document/` uses MyST Markdown (`.md`) only.
Use MyST directives for Sphinx features such as `toctree`; do not add RST
source files. Public C and C++ API comments live with their component headers;
Doxygen emits XML and Breathe renders it inside both Sphinx language trees.
User-visible documentation changes should update both English and simplified
Chinese pages in the same pull request.

Portable code must not include Zephyr, POSIX, ROS, AimRT, XRobot, libxr or STM32
HAL headers. Put platform behaviour behind an existing Interface and add an
Adapter only when behaviour actually varies.

Public Interface changes require a compatibility note in `CHANGELOG.md` and a
contract test. Do not add unbounded allocation to Zephyr hot paths.
