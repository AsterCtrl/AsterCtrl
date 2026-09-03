Packages and plugins
====================

A Package declares exported Modules, types, plugins, platform support, resource
bounds, dependencies and license. The package manager resolves exact revisions
into ``package.lock``.

Linux loads Module Bundles and Core Plugins through distinct stable C ABI
entrypoints. Module Bundles create business Modules; Core Plugins expose named,
versioned backend function tables with explicit unload ownership. Zephyr uses
the same manifest but generates a static registry. Package manifests are
declarative; resolving a package never executes arbitrary repository Python.

CMake ``FetchContent`` is reserved for pinned host dependencies and test tools.
Zephyr modules are pinned through ``west.yml``.
