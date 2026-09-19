# 双平台核心收敛：实施审计

> 历史快照：2026-09-05。本文记录当时工作树的审计结果，不是当前分支状态，也不是
> Release Notes 或实板验收。当前用户流程以 README、CLI 参考和最新 CI 记录为准。

审计日期：2026-09-05。范围：当时的未提交工作树。

## 结论

普通 Linux 已有 `Module → Package → runtime.yaml → aster run` 的配置驱动路径，
不再要求用户维护两张图。**下一阶段计划尚未全部完成**：v1alpha3 部署编译器、
Zephyr 静态生成和新 Runtime 的跨节点后端仍缺少集成闭环。

“通信关系”和“部署放置”保留为内部视图的设计，不意味着用户还要写
`application.yaml` 与 Port 连接表。当前 `VisitGraph` 只枚举 Module；旧
`aster graph` 仍是 v1alpha2 Application 编译器。完整运行时通信视图尚未交付。

## 按计划核对

| 范围 | 当前实现与证据 | 剩余工作 |
| --- | --- | --- |
| C ABI 与薄 C++ facade | ABI 2；公共 Ref 仅包装 C 表；Runtime 实现移出 Module SDK；纯 C 与 C++ 契约测试 | 继续审计各平台失败/ISR 路径，不把头文件存在当作服务实现 |
| Package 与实例隔离 | Linux 自动入口、静态/动态混合；独立配置、日志身份、namespace/remap、执行器；类型不符的工厂结果被拒绝 | 同一 Manifest 生成 Zephyr 静态入口仍未实现 |
| Linux 启动与服务 | 原生 YAML 解析；串行/线程池；有类型配置/参数；日志级别、Clock、Allocator；失败回滚与卸载测试 | Clock/测试替身尚不能通过新启动器选择；完整健康与通信关系查询未交付 |
| Local Channel/RPC | 自有消息存储、实例名称解析、广播、类型冲突、超时、背压、并发和关闭取消测试 | 这些是进程内语义；不能据此宣称跨节点实际注册核对完成 |
| CorePlugin | 独立 ABI、动态加载、版本与接口查询测试 | `core_plugin_main.h` 只有查询入口，生产生命周期及后端注册尚未接入 `NodeRuntime` |
| 配置/部署编译 | Runtime 与 Package 已用 v1alpha3；旧工具会提示迁移 | `graph.py`、`models.py`、`emitters.py` 和部署 Schema 仍为 v1alpha2；缺新的 Parser/Typed IR/Resolver/平台输出闭环 |
| Zephyr | 保留有界 Registry、Executor、设备 Adapter 和旧生成路径 | 新类型化静态配置、资源表、入口、同源 Module 运行验收未完成；Parameter 服务仍不可用；需执行热路径无堆/真实 ISR 测试 |
| CAN/USB | 保留 CAN Channel/RPC、USB Channel、协议与 pseudo-TTY 回归 | 新启动器仅接受 Local，跨节点部署/握手未接通；USB RPC 明确不支持 |
| 通信兼容身份 | 有 Schema 与有界编码分析 | 旧 emitter 仍把完整 `lock["content_hash"]` 用作 Deployment 握手身份；尚未与业务参数、日志、产物摘要分离 |
| 硬件模型收敛 | 新 Host 不要求算法使用 HardwareManager | 旧 Hardware/Capability/Provider Schema、生成逻辑、SDK 与示例仍在；须在替代路径测试完成后删除 |
| CLI 与构建 | 最小 Linux init；Package 自动生成；移除自建依赖管理；`cmake/`、固定 yaml-cpp、预置源码、C++20 导出和独立消费者 | Zephyr/跨节点 init 选项未实现；`aster build` 仍是旧部署构建；不应假装它已消费新 runtime.yaml |
| 部署运维 | Bundle 摘要、分阶段目录、current/previous 切换、systemd 模板 | 部署工具仍依赖旧 Deployment/Inventory；尚未只消费构建产物描述；不会启动服务或烧录 MCU |
| 文档与发布 | Doxygen XML 集成双语 Sphinx；默认教程与旧路径边界已修正 | 本快照之后官网已迁移；Linux/Zephyr CI 与实板门禁需以当前运行记录和实测证据为准；依赖 SBOM 需覆盖 yaml-cpp 等非 Python 依赖 |

## 本次继续修复的遗漏

- `aster validate runtime.yaml` 原先无法识别默认 init 产物；现在复用原生解析器，
  只校验配置，不加载动态库。`run --check` 才执行 Module 初始化与注册封闭。
- systemd 模板仍调用已经删除的 `run --execute`；现在调用安装后的
  `aster_runtime --config`，并由 CMake 填入安装路径。集成测试运行其实际启动命令；
  本机未运行 systemd 服务管理器。
- 动态工厂返回的 `Info().type` 原先未与导出类型核对；现在返回 `TypeMismatch`
  并销毁不匹配对象，现有合法实例保持不变。测试先复现错误接受，再验证拒绝。
- 中英文概念、Package、平台、部署、调试、协议、API 和 CLI 页面不再把旧双图、
  泛化 Provider 或未集成的插件当成当前默认功能。旧示例明确标为回归路径。
- 为旧 graph/resolve/build 增加迁移提示。保留历史开发日志，明确其决策已被后续
  收敛计划替代，而不是改写历史。

## 验证边界

本次 Host 回归在 macOS 执行，不等价于 Linux x86_64/arm64 验证。完整结果记录在
[开发日志](2026-09-05-core-convergence.md)。其中 SocketCAN 测试在非 Linux 平台跳过
真实 `vcan` 路径；pseudo-TTY 也不证明 USB 枚举或实物互通。

没有在本机找到可用的 Zephyr checkout/SDK，本次未执行 `native_sim`、QEMU、
`dev_c`、`mc02` 编译和尺寸检查。双板 console、clock、CAN loopback、UART、SPI、
watchdog，以及跨节点 CAN 丢包/重启实测仍是正式版门禁。

本快照中的工作流配置不代表当时已获得远端绿色结果；后续提交、远端运行和发布状态
不由本历史页追踪。旧归档和机器人业务代码不在本文范围内。

## 下一批顺序

1. 完成 CorePlugin 生命周期与真实后端注册，测失败回滚、在途任务关闭和卸载安全。
2. 实现可选 v1alpha3 Deployment：实例放置、Topic/RPC 节点契约、稳定通信身份；
   启动前核对真实注册。不要重新引入 Port-to-Port 图。
3. 从同一 Manifest 生成 Zephyr 静态入口及有界配置/资源，跑通同源 Module，
   再删除旧 Provider/Hardware 图编译路径及重复示例。
4. 接入已有 CAN/USB，更新独立产物 Bundle；完成 Linux/Zephyr 构建与硬件门禁。

ROS Bridge、Linux IP Transport、录制回放、分布式监控和完整 SIL/PIL 是明确后移的
范围，不应为了清空待办而在本轮补建。
