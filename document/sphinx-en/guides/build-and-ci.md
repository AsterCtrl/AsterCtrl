# Build and CI

Host builds require Python 3.12, uv, CMake 3.28+, Ninja and a C++20 compiler.
The project pins Python dependencies in `uv.lock` and exposes target-based
CMake packages under the `aster::` namespace. The standard presets are
`host-debug`, `host-clang`, `host-asan` and `host-tsan`.

```console
uv sync --frozen --all-groups
cmake --preset host-clang
cmake --build --preset host-clang
ctest --preset host-clang --output-on-failure
```

Host-only dependencies and test tools may use CMake `FetchContent` only when
their commit or archive hash is fixed. Zephyr, HALs, modules and board support
are resolved exclusively by `west.yml`. The Host offline check requires both
Python dependencies and yaml-cpp source to be prepared first, then repeats the
build with network access disabled. This does not prove an offline Zephyr build.

Configured pull-request gates cover Clang 18 format and tidy, GCC and Clang warnings, Host tests,
ASan/UBSan, a separate TSan job, x86_64 and arm64 builds, bounded-Protobuf
vectors, graph negative fixtures, deterministic generation, Sphinx/MyST
documentation, dependency license and pin audits, and Zephyr Twister plus board
size checks. Files under `document/` use Markdown exclusively; CI rejects RST
sources so future contributions keep one documentation syntax.

A workflow definition is not a passing result. Local evidence and unexecuted
Linux/Zephyr gates are recorded in the [implementation audit](../development/convergence-audit.md).

Doxygen extracts XML from Module headers under `src/interface` and the separate
Runtime SDK under `src/runtime`; Breathe renders that XML
inside the English and simplified-Chinese Sphinx sites. Doxygen does not emit a
separate HTML site:

```console
cmake -E make_directory build/document/doxygen
doxygen document/doxygen/Doxyfile
uv run python -m sphinx -W --keep-going -b html \
  document/sphinx-en build/document/html
uv run python -m sphinx -W --keep-going -b html \
  document/sphinx-cn build/document/html/zh_CN
```
