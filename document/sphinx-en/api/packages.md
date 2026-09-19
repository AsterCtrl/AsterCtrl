# Package and Core Plugin ABI

Package entrypoints are generated from declarative Module registration. Core
Plugins use a separate ABI and cannot be loaded as Module Packages.

The v1alpha3 Manifest currently generates Linux Package entries; its Zephyr
static-entry counterpart is pending. CorePlugin loading and interface-query
tests exist, but production lifecycle and backend registration are not wired
into the new launcher. See [Packages](../concepts/packages.md).

```{doxygenfile} pkg_main.h
:project: AsterCtrl
:sections: func
```

```{doxygenfile} core_plugin_main.h
:project: AsterCtrl
:sections: innerclass func
```
