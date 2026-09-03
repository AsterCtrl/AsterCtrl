Deployment workflow
===================

The toolchain reads inputs in this order:

.. code-block:: text

   Package / Application / Deployment / Hardware / Inventory
                              |
                    validate, resolve, lock
                              |
                 generated Linux or Zephyr inputs
                              |
                         CMake or west

``inventory.yaml`` contains host addresses, serial paths, debug-probe names and
labels, but never credentials. A physical target may also declare a stable
``serial_number`` (for example ``DEV-C-001``); it is carried into each
Deployment Action so an Adapter can verify device identity before flashing.
The field is optional because local and development targets may not expose a
hardware serial number. ``aster deploy plan`` is always read-only and
shows exact sources, destinations and mechanisms. ``apply`` requires the
explicit ``--execute`` flag and refuses a target mechanism for which no Adapter
is configured.

``aster codegen`` writes a versioned ``deployment.bundle.yaml`` beside the
Deployment Lock. The bundle records the SHA-256 and byte size of the lock and
every generated file. Its own ``bundle_digest`` is calculated from a canonical
representation of those records. ``deploy plan`` rejects missing, unexpected,
modified or symlinked files before presenting an action.

Local ``deploy apply --execute`` copies each node into a private staging
directory, verifies every copied byte, renames the stage into an immutable
``releases/<bundle-digest>`` directory, and atomically changes the ``current``
symlink. The former release remains available through ``previous`` and is also
recorded in ``.aster-deploy-state.yaml`` for the documented rollback procedure.
``deploy status`` reports the active Deployment ID and bundle digest from that
state.

SSH deployment uses only argv-based ``ssh`` and ``scp`` invocations with
``BatchMode`` enabled. Authentication comes from the operator's SSH agent or
SSH configuration; passwords, private keys and tokens are not fields in
``inventory.yaml`` and are never written to deployment state. Remote commands
are not started unless ``--execute`` is present. Serial and debug-probe flashing
are deliberately rejected in v0.2.

SSH only moves and activates artifacts. Application data still uses the
resolved Local, CAN/SocketCAN or USB routes.

Executor policies are declared below each node's ``executors`` map. The key is
an Application domain. ``serial`` is portable across Linux and Zephyr;
``worker_pool`` is a Linux-only policy. Optional ``workers``, ``priority``,
``stack_bytes`` and ``queue_depth`` values must satisfy every task placed in the
domain and become immutable lock data.

.. code-block:: shell

   aster codegen workspace.yaml deployment.yaml build/generated
   aster deploy plan deployment.yaml inventory.yaml build/generated
   aster deploy apply deployment.yaml inventory.yaml build/generated --execute
   aster deploy status inventory.yaml
   # Remote status also requires explicit process execution:
   aster deploy status inventory.yaml --execute
