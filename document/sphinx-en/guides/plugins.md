# Packages and plugins

Dependencies belong to CMake, west and uv. `aster package add/remove/list/lock`
has been removed. A v1alpha3 Package Manifest describes exported Module types,
C++ classes, headers, sources and platform support, not dependency resolution.

```yaml
api_version: aster.dev/v1alpha3
kind: Package
metadata: {name: demo, version: 0.1.0, license: Apache-2.0}
spec:
  modules:
    - type: demo.Hello
      class: demo::Hello
      header: src/hello.hpp
      sources: [src/hello.cpp]
      platforms: [linux, zephyr]
```

There is no mandatory Port declaration. Paths are relative to the Manifest and
must stay inside the Package. CMake uses `aster_add_package(demo MANIFEST package.yaml)`
to generate the dynamic entry; add the application's own dependencies with ordinary
`target_link_libraries`. The generated target requires only the Module and Package
Interfaces. No repository-owned Python plugin is executed. The current helper selects
Linux exports; static v1alpha3 Zephyr generation is still pending.

There are two plugin boundaries:

Module Package

: Supplies business Modules. Linux creates them dynamically through the C ABI.
  `aster codegen` owns the Linux factory glue, so its catalog, create and
  destroy symbols require no handwritten loader code in business Modules.

Core Plugin

: Intended to supply a platform or transport Implementation behind an existing
  Interface. Its production lifecycle/backend registration is not wired into the
  new configuration-driven Runtime yet. Loader tests alone are not acceptance.

Runtime registration closes before Start. Cross-node communication contracts and
static Zephyr resources belong to optional deployment compilation, not to the
ordinary Linux Package build. See [migration status](runtime-v3.md).
