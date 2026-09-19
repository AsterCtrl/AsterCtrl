# Core services

These are extracted interfaces, not a claim that every platform supplies every
service. The new Host provides typed configuration and parameters; the retained
Zephyr Parameter service is unavailable. `HardwareManagerRef` remains for the
legacy deployment migration and is not supplied by the new Host NodeRuntime.
See [runtime contracts](../concepts/runtime.md).

## C++ facade

```{doxygenclass} aster::ConfiguratorRef
:project: AsterCtrl
:members:
```

```{doxygenclass} aster::LoggerRef
:project: AsterCtrl
:members:
```

```{doxygenclass} aster::ExecutorRef
:project: AsterCtrl
:members:
```

```{doxygenclass} aster::ParameterRef
:project: AsterCtrl
:members:
```

```{doxygenclass} aster::ClockRef
:project: AsterCtrl
:members:
```

```{doxygenclass} aster::AllocatorRef
:project: AsterCtrl
:members:
```

```{doxygenclass} aster::HardwareManagerRef
:project: AsterCtrl
:members:
```

## C ABI tables

```{doxygenstruct} aster_configurator_base_t
:project: AsterCtrl
:members:
```

```{doxygenstruct} aster_logger_base_t
:project: AsterCtrl
:members:
```

```{doxygenstruct} aster_executor_base_t
:project: AsterCtrl
:members:
```

```{doxygenstruct} aster_parameter_base_t
:project: AsterCtrl
:members:
```

```{doxygenstruct} aster_clock_base_t
:project: AsterCtrl
:members:
```

```{doxygenstruct} aster_allocator_base_t
:project: AsterCtrl
:members:
```

```{doxygenstruct} aster_hardware_manager_base_t
:project: AsterCtrl
:members:
```
