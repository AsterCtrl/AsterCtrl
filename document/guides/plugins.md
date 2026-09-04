# Packages and plugins

`aster package add`, `remove`, `list` and `lock` operate on declarative
Package Manifests. A release lock accepts Git dependencies only at immutable
commit revisions and records source digests. Dependency resolution never imports
or executes Python from a package repository.

There are two plugin boundaries:

Module Bundle

: Supplies business Modules. Linux creates them dynamically through the C ABI;
  Zephyr generates equivalent static Module slots from the same Manifest.

Core Plugin

: Supplies a versioned platform or transport Implementation behind an existing
  Interface. A package must explicitly export the backend named by a Deployment.

Plugins may not add a new Module Instance, route or peer after resolution. Any
capability they provide must be declared, validated and represented in the
Deployment Lock before build.
