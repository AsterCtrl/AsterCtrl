# 架构

AsterCtrl 是独立的机器人控制框架；Linux 与 Zephyr 是操作系统，AsterCtrl 不是
另一个操作系统。目标是可移植 Module 使用同一份 C++ 源码，针对不同平台重新编译，
而不是跨 MCU/SoC 复用同一份二进制。

业务 Module 实现 ModuleBase，通过 CoreRef 使用配置、日志、执行器、Channel、
RPC、参数、时钟和分配器。普通 Linux 用 runtime.yaml 启动；跨节点/Zephyr 的
可选部署编译仍在迁移。见 [配置与部署](graphs.md)。

算法通过 Channel/RPC 与 Driver Module 交互；设备访问留在 Driver 内。
不要求算法声明通用硬件 Capability 或获取 HardwareManager。
旧硬件注册表与 Provider 编排尚未从迁移路径彻底删除，不是新算法的默认契约。

公共 SDK 位于 src/interface，只包含 canonical C ABI 和薄 C++ facade。
Runtime、Supervisor、Registry、平台实现和 Transport 内部结构位于独立
Runtime SDK 与实现目录，应用 Package 不被迫链接它们。

框架负责生命周期、注册封闭、调度、消息所有权和失败回滚。
Linux 可以动态分配；Zephyr 路径必须有界、无异常、无 RTTI，热路径不得依赖堆。
当前 Linux 核心已实现，v1alpha3 Zephyr 同源部署尚待验证；不能把目标约束当作
已经通过的硬件验收。

AimRT 只作为设计参考，不是运行依赖。ROS/AimRT Bridge、Linux IP Transport、
录制回放、分布式监控和完整仿真/PIL 留到后续阶段。
