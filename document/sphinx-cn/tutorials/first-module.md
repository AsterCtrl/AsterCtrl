# 编写第一个 Module

Module 在普通 C++20 中实现业务行为。Topic/RPC 注册发生在 `Initialize()`，工作从
`Start()` 开始；当前 Linux 直接使用这些注册结果，不维护独立的 Port 表：

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

`Command` 的 TypeSupport 由你的有界 `.proto` 生成。Module 包含该生成头文件和
`aster_module_cpp_interface/module.hpp`。上面是生命周期示意，假定消息已经定义。

Package 导出 `demo.Controller` 后，新 Host Runtime 使用如下配置创建实例：

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

在 Initialize 中使用 `core.configurator().Get("gain", gain)` 读取 `double`，并处理
缺失、类型错误等状态。新 Runtime 不再将 C++ 对象内存或隐式 JSON `config` blob
作为普通配置接口。

发布时使用 `output_.Publish(message)`；执行身份和时间戳由 Runtime 提供。这里的
`command` 会展开为 `/robot/command`。Module Package 链接
`aster::module_cpp_interface` 和 `aster::pkg_c_interface`，不链接 `aster::core`。

`examples/common/portable_pubsub.hpp` 同时被 Host 测试和 Zephyr smoke 应用引用。
本次 v1alpha3 的两端运行与板卡迁移仍需验证；Manifest 和模板迁移状态见
[迁移说明](../guides/runtime-v3.md)。
