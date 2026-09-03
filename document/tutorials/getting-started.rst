Getting started
===============

Install Python 3.12, uv, CMake 3.28+, Ninja and ``protoc``, then run:

.. code-block:: console

   uv sync --all-groups
   uv run aster doctor
   cmake --preset host-debug
   cmake --build --preset host-debug
   ctest --preset host-debug

Create, resolve and run a generated single-node project:

.. code-block:: console

   uv run aster init hello-aster
   uv run aster codegen hello-aster/workspace.yaml \
     hello-aster/deployment.sim.yaml hello-aster/build/generated
   cmake -S hello-aster -B hello-aster/build/host -G Ninja \
     -DASTERCTRL_SOURCE_DIR="$PWD" \
     -DASTER_GENERATED_DIR="$PWD/hello-aster/build/generated/nodes/app"
   cmake --build hello-aster/build/host
   hello-aster/build/host/aster_app

The generated YAML and Module source are ordinary project inputs and may be
committed. ``ASTERCTRL_SOURCE_DIR`` is convenient while developing against a
checkout; installed SDKs are found with ``find_package(AsterCtrl)`` instead.
