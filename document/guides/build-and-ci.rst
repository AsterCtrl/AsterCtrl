Build and CI
============

Host builds require Python 3.12, uv, CMake 3.28+, Ninja and a C++20 compiler.
The project pins Python dependencies in ``uv.lock`` and exposes target-based
CMake packages under the ``aster::`` namespace. The standard presets are
``host-debug``, ``host-clang``, ``host-asan`` and ``host-tsan``.

.. code-block:: console

   uv sync --frozen --all-groups
   cmake --preset host-clang
   cmake --build --preset host-clang
   ctest --preset host-clang --output-on-failure

Host-only dependencies and test tools may use CMake ``FetchContent`` only when
their commit or archive hash is fixed. Zephyr, HALs, modules and board support
are resolved exclusively by ``west.yml``. After ``uv sync`` and ``west update``
the CI repeats representative builds with network access disabled.

Pull requests run Clang 18 format and tidy, GCC and Clang warnings, Host tests,
ASan/UBSan, a separate TSan job, x86_64 and arm64 builds, bounded-Protobuf
vectors, graph negative fixtures, deterministic generation, Sphinx, dependency
license and pin audits, and Zephyr Twister plus board size checks.
