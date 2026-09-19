# Write a first Module

A Module implements business behaviour in ordinary C++20. It registers Topic/RPC
endpoints in `Initialize` and begins work in `Start`. Ordinary Linux usage does
not require a separate Port declaration:

```cpp
class Controller final : public aster::ModuleBase {
 public:
  aster::ModuleInfo Info() const noexcept override {
    return {"controller", "demo.Controller", "demo", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return output_.Bind(core.channel(), "command");
  }

  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}

 private:
  aster::Publisher<Command> output_;
};
```

The `Command` TypeSupport is generated from your bounded `.proto` file. Include
its generated header and `aster_module_cpp_interface/module.hpp` in the Module.
The example is a lifecycle sketch; it assumes that message definition exists.

Once the Package exports `demo.Controller`, the new Host Runtime creates an
instance using this configuration:

```yaml
api_version: aster.dev/v1alpha3
aster:
  packages: [{name: demo, path: ./build/libdemo.so}]
  modules:
    - name: controller
      type: demo.Controller
      package: demo
      namespace: /robot
      config: {gain: 1.5}
```

Use `core.configurator().Get("gain", gain)` to read a `double` in Initialize.
Handle missing and incorrect types explicitly. In the new Runtime, configuration
is typed data, not C++ object bytes or an implicit JSON `config` blob.

Publish with `output_.Publish(message)`; execution identity and timestamps are
supplied by Runtime. This example's `command` Topic expands to `/robot/command`.
Module Packages link `aster::module_cpp_interface` and `aster::pkg_c_interface`,
not `aster::core`.

See `examples/common/portable_pubsub.hpp` for one source file compiled by the
Host test suite and referenced by the Zephyr smoke application. The current
v1alpha3 board/runtime migration still needs execution validation. See the
[migration status](../guides/runtime-v3.md), including the pending Manifest/template migration.
