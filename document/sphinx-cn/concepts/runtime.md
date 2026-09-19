# Runtime 契约

每个业务组件实现四个方法的 `aster::ModuleBase` Interface：

```cpp
ModuleInfo Info() const noexcept;
Status Initialize(CoreRef core);
Status Start();
void Shutdown() noexcept;
```

`Initialize()` 是唯一注册阶段。Module 从 `CoreRef` 获取 Configurator、Logger、
Executor、Channel、RPC、Parameter、Clock 和 Allocator。全部
Module 初始化后，Runtime 先封闭所有 Registry，再进入 `Start()`。

新 Host Runtime 的实例隔离、名称解析、异步交付和关闭语义见
[v1alpha3 说明](../guides/runtime-v3.md)。HardwareManager 仅暂留于迁移中的旧路径，
不再是新 Host 算法 Module 的必选服务。Linux 的初始化/启动异常在 ABI 适配器中
转换为错误状态；Zephyr 仍以无异常方式构建。

生命周期固定为加载、初始化、封闭、启动、运行和逆序停止。初始化、封闭或启动失败
都会逆序清理已初始化 Module。Host 与保留的 Zephyr 执行器均实现启动 gate：所有
Module 启动成功前，排队任务不会运行；停止时先拒绝并清空任务、唤醒延时等待并回收
工作线程，之后才调用 Module 的 `Shutdown()`。

这不代表 v1alpha3 Zephyr 部署已经完成。Host 已接通有类型的配置和可变参数；旧
Zephyr Core 的 `ParameterRef` 仍不可用，新的有界静态配置/参数路径尚待迁移验证。
详细状态见[实施审计](../development/convergence-audit.md)。

`aster_module_base_t` 是唯一的生命周期载体。`ModuleBase` 只把 C++ 虚函数适配到该
C ABI，Runtime 只保存 `ModuleRef`，所以静态 Zephyr Module 与动态 Linux Module
不会走两套逻辑。反方向由 Runtime 侧的 `CoreAdapter` 持有 C 服务表，`CoreRef`
只是借用这些表的薄 C++ facade。

C++ STL 对象、异常和所有权不明确的对象不会跨 Linux 插件边界。根接口包含 ABI
版本和结构大小，嵌套服务表包含结构大小；兼容扩展只能在尾部追加字段。Zephyr
热路径使用有界队列和固定存储，ISR 只把工作移交给线程上下文。
