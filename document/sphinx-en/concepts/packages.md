# Packages, Module types and instances

A Package is a buildable collection of exported Modules, not a custom dependency manager.

The v1alpha3 package.yaml describes identity, version, license and each Module's
exported type name, C++ class, header, sources and platforms. It requires no Port
declarations or dependency lock. CMake, west and uv own Host, Zephyr and Python
dependencies respectively.

- **Module type**: for example demo.Hello, implemented by one class.
- **Module instance**: for example left and right, configured separately in runtime.yaml.
- **Package**: for example demo, built as a Linux dynamic library that creates instances.

`aster_add_package(demo MANIFEST package.yaml)` generates registration and C ABI
catalog/create/destroy entries. Business code implements ModuleBase, not export
macros or a custom launcher. The generated target links Module/Package Interfaces
only; the C++ Interface target propagates C++20.

Static Modules use NodeRuntime's RegisterModule; dynamic Modules are constructed
from Packages. Both use the ModuleRef lifecycle. See the [Package guide](../guides/plugins.md)
for usage and the Manifest.

CorePlugin is a separate extension entry. Only loading and interface-query
foundations exist; production lifecycle/backend registration is not connected
to the new Runtime. Static Zephyr generation from the v1alpha3 Manifest is also
pending. Neither is a completed feature.
