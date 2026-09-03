Linux Runtime
=============

Linux nodes can combine statically constructed Modules with versioned ``.so``
Module Bundles. ``aster::platform::linux::Supervisor`` owns the Runtime
lifecycle, checks the Deployment ID, loads plugins, exposes graph inspection and
performs reverse-order cleanup.

A Module package returns an ``AsterModuleBundlePluginV1`` descriptor from the
``aster_module_bundle_v1`` entrypoint, which creates an
``AsterModuleBundleV1``. A Core Plugin instead returns an
``AsterCorePluginV1`` from ``aster_core_plugin_v1`` and exposes named,
versioned C function tables for Transport or Runtime backends. The two loaders
never treat one entrypoint as the other.

Both descriptors contain ABI versions, structure sizes and explicit release
callbacks. Function tables returned by a Core Plugin are borrowed until that
plugin is closed and begin with their own version and structure size. The
loaders reject missing functions, undersized structures, duplicate Module
names and ABI or TypeSupport mismatches before the node starts.

``PluginLoader::Close`` is the final ownership boundary, not a cancellation
operation. The owning Runtime must first call ``Shutdown`` on every Module and
the application must stop or drain every Executor callback and RPC completion
that can enter the shared object. Only then may it release the Module bundle,
release plugin state and call ``dlclose``. Likewise, users of a Core Plugin
must release all borrowed backend tables before ``CorePluginLoader::Close``.

Linux Adapters include a thread Executor, monotonic Clock, Logger, SocketCAN,
TTY-backed USB framing and fake Clock/Hardware services for simulation. A
packaged systemd template is installed as ``aster-node@.service`` on Linux.
