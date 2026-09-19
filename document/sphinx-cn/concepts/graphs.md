# 配置、通信关系与部署

普通 Linux 应用不再要求用户维护 Application Graph 和 Deployment Graph，
也不需要 application.yaml、Module Port 连接表或 Deployment Lock。

## 当前已实现：runtime.yaml

`runtime.yaml` 以 `aster` 为根配置，选择 Package、Module Instance、执行器、
日志、通信后端和实例业务参数。使用流程是：

```text
编写 Module → CMake 构建 Package → runtime.yaml → aster run
```

Module 在 `Initialize()` 中注册 Topic 和 RPC。名称按“实例 namespace 展开、
一次精确 remap、注册”解析。同名且同类型的 Topic 表示共享广播；
RPC 通过逻辑服务实例名和方法名区分目标。注册完成后封闭 Registry，再启动 Module。

Linux 在启动时用 yaml-cpp 读取配置，实例业务参数采用类型化访问；
不再把参数预先固化为生成代码中的任意 JSON/对象内存。

## 两种内部视图，不是两份强制用户输入

- **通信关系**：来自实际 Topic/RPC 注册，以及跨节点通信契约。
- **部署关系**：来自 Module Instance 到 Node 的放置、节点平台和链路配置。

这是目标架构的职责划分，不表示两套完整 Graph 查询工具已经交付。
目前 NodeRuntime 的 `VisitGraph` 只枚举 Module 类型与实例；
`aster graph` 仍是旧 v1alpha2 Application 编译器，不能查询新 Runtime 的通信图。
离线工具不能从任意 C++ 源码推导全部注册；`aster run --check` 会实际加载、
初始化并核对本节点注册，不能替代尚未实现的跨节点契约核对。

## 可选跨节点和 Zephyr 部署：尚在实现

目标是由可选 `deployment.yaml` 引用 Runtime 配置，指定实例放置、Linux/Zephyr
平台、板卡、节点资源和通信链路。跨节点约束按 Topic/RPC 名称、类型、收发节点及
容量声明，不重新引入 Module Port-to-Port 图。

业务配置保留在 Runtime 配置中；节点配置只承载平台相关策略，不提供任意 YAML
深度覆盖机制。Zephyr 需要构建期生成静态入口、资源表、Kconfig 和 overlay，
不在 MCU 上解析 YAML 或开放式发现新拓扑。**v1alpha3 部署编译尚未完成。**

## 旧双图代码的状态

仓库中旧 application.yaml、module.yaml Port 声明、Provider/Capability、
Hardware Profile、旧 Resolver 和部分示例仍用于迁移回归。
它们不是新 Linux 用法，也不是第二套长期支持方案；替代路径通过测试后才能删除。

从 [快速开始](../tutorials/getting-started.md) 使用新路径。
完整缺口见 [实施审计](../development/convergence-audit.md)。
