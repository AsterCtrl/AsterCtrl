# Write a first Module

A Module declares ports and resource bounds in `module.yaml` and implements
business behaviour in ordinary C++20. The implementation registers everything
in `Initialize` and begins work in `Start`:

```cpp
class Controller final : public aster::Module {
 public:
  aster::ModuleInfo Info() const noexcept override {
    return {"controller", "demo.Controller", "demo", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    return output_.Bind(core.channel(), "command");
  }

  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}

 private:
  aster::ClockRef clock_;
  aster::Publisher<Command> output_;
};
```

The `Command` TypeSupport is generated from a bounded `.proto` file. The
Module Manifest names the CMake target, class and header and states supported
platforms, typed ports, tasks, queue depths and stack bounds. It also owns the
JSON Schema for per-Instance configuration and declares static memory and flash
needed by one Instance:

```yaml
spec:
  parameters:
    type: object
    required: [gain]
    properties:
      gain: {type: number, minimum: 0}
    additionalProperties: false
  resources:
    static_ram_bytes: 512
    flash_bytes: 4096
```

Application YAML creates Instances of this type and supplies only values that
pass that Schema; it does not repeat implementation details. The resolver adds
each placed Instance's static RAM to its executor stacks, checks RAM and flash
budgets, and records the totals in `deployment.lock.yaml`. Both Linux and
Zephyr compositions embed the validated configuration as canonical JSON, so
configuration does not depend on YAML parser behaviour at Runtime. During
`Initialize`, each Module receives its own Configurator view: reading the
`config` key returns that Instance's canonical JSON bytes. Other keys fall
back to the Configurator supplied by the host Runtime. The generated storage is
fixed-capacity and sealed before Module initialization on both platforms.

See `examples/common/portable_pubsub.hpp` for one source file compiled by the
Host test suite and by the Zephyr `native_sim` and QEMU smoke application.
