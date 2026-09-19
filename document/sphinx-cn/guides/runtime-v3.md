# 配置驱动的 Linux Runtime：迁移状态

这是下一版 alpha 的实施说明，不是完整双平台验收声明。

## 当前可用路径

```text
C++ Module → Package 动态库 → runtime.yaml → aster run --config runtime.yaml
```

普通 Linux 不需要 `application.yaml`、Port 连接表或 Deployment Lock。
下面的配置假定应用已经构建并导出了 `robot.Controller`：

```yaml
api_version: aster.dev/v1alpha3
aster:
  executors:
    - {name: control, type: serial, queue_capacity: 64}
    - {name: io, type: thread_pool, threads: 2, queue_capacity: 128}
  logging: {level: info}
  channel: {backends: [local]}
  rpc: {backends: [local]}
  packages:
    - {name: robot, path: ./build/librobot.so}
  modules:
    - name: left
      type: robot.Controller
      package: robot
      executor: control
      namespace: /left
      remap: {/left/state: /robot/state}
      config: {gain: 2, label: "left motor"}
      parameters: {target: 0.0}
```

动态库路径相对于配置文件所在目录。`enabled: false` 的实例不创建、不初始化。
`log_level` 可覆盖该实例的日志级别；实例身份与 Module 类型分别保存。
目前新启动器只接通 Local 后端；其他 backend 名称会被明确拒绝。

嵌入式 Host 使用者可包含 `aster_runtime/platform/linux/runtime.hpp`，构造
`aster::platform::linux::NodeRuntime`，先 `RegisterModule(instance_name, module)`
注册借用的静态对象，再调用 `Initialize(yaml)`、`Start()`、`Shutdown()`。
静态对象的配置省略 `package`，它必须比 Runtime 活得更久。

## 接口变化

- ABI 已升级到 2，旧动态 Package 必须重新构建。
- Module SDK 的 Ref 仅持有 C 函数表指针，不再持有内部 Backend 指针。
  Runtime/Registry/Transport 实现在独立的 Runtime SDK 中。
- `core.configurator().Get("gain", value)` 按类型读取；缺失返回 `NotFound`，
  类型错误返回 `TypeMismatch`。嵌套配置使用点分路径，例如 `controller.gain`。
  当前支持 null、bool、64 位整数、有限浮点和字符串；数组不被悄悄转成字符串。
- 字符串配置借用到 Shutdown；可变参数读取必须提供容量足够的调用者缓冲区。
  参数更新保留原类型，不创建新键。
- `logger.Write(level, text)`、`publisher.Publish(message)`、`executor.TryPost(work)`
  和 RPC 调用无需构造 `ExecutionContext`。
- `clock.NowNs(now)`、`clock.GetDomain(domain)` 返回 `Status`，失败不覆盖输出。

## 通信和关闭语义

名称顺序是 namespace 展开、一次精确 remap、注册。绝对名称不加 namespace；
不支持通配符或递归 remap。同名且同类型 Topic 广播到全部订阅者。
类型冲突在初始化期间报错，注册表在 Start 前封闭。

Channel 将编码后的消息复制一次并在订阅者之间共享所有权，在订阅实例的执行器上
回调。满队列返回 `CapacityExceeded`；广播逐个目标入队，某个目标失败不会撤回
其他目标已接受的消息，因此不能把重试当成原子广播。

RPC 使用独立的服务实例名与方法名。`client.Bind(core.rpc(), "right")` 选择逻辑
服务实例；新 Runtime 中，server 未指定实例名时使用所属 Module 实例名。两者的
逻辑实例名同样经过 namespace/remap。请求在服务端执行器处理，正常完成与超时在
客户端执行器回调。接受请求前预留完成回调队列位置；不能预留则立即返回背压。

Shutdown 先关闭通信入口，再停止执行器并等待正在执行的回调，丢弃未执行任务，
取消未完成 RPC，最后逆序停止 Module、销毁动态对象并卸载 Package。关闭取消回调
运行于 shutdown 线程，此时 Module 仍然存在；Shutdown 必须从控制线程调用。

## 仓库内验证

`aster validate runtime.yaml --runtime /path/to/aster_runtime` 只用原生解析器校验配置，
不加载 Package。`aster run --config runtime.yaml --check` 会加载 Package、执行
`Initialize()`、封闭注册表再 `Shutdown()`，因此可能产生 Module 初始化的副作用。

[快速开始](../tutorials/getting-started.md) 已使用 `aster init` 与
`aster_add_package` 构建最小用户 Package。以下则使用仓库内的测试 Package
检查通用启动程序：

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
uv run aster run --runtime build/host/aster_runtime \
  --config build/host/configured-runtime-test.yaml --duration-ms 50
ctest --test-dir build/host --output-on-failure
```

yaml-cpp 使用固定归档 SHA-256，通过 FetchContent 获取。预置源码可传入
`-DFETCHCONTENT_SOURCE_DIR_YAML_CPP=/path/to/yaml-cpp`，随后使用
`-DFETCHCONTENT_FULLY_DISCONNECTED=ON` 构建。

## 尚未完成

- CorePlugin 的生产生命周期和可选择后端注册。
- v1alpha3 可选 Deployment 编译、Zephyr 静态入口与同源算法运行验收。
- CAN/USB 与新启动器的接线、通信契约身份和实际注册核对。
- 泛化 Hardware/Capability/Provider 编排删除及官网完整迁移。
- Linux GCC、Zephyr 两板编译和真实硬件 smoke。

默认模板和 Package 入口已使用 v1alpha3，旧依赖管理命令已移除。
旧部署示例和命令暂留用于回归，并非第二套长期支持方案；不会把它们静默当成
v1alpha3 配置执行。硬件和发布门禁未完成前，不宣称正式版验收通过。

逐项代码核对与后续顺序见[实施审计](../development/convergence-audit.md)。
