# xrobot-runtime

Portable runtime contracts for the control-2027 distributed robot framework.

This repository owns Module lifecycle, execution contexts, executors, messaging,
parameters, diagnostics, and compatibility adapters. Platform integrations live
behind backend targets; portable public headers do not include libxr, HAL, RTOS,
or POSIX types.

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```
