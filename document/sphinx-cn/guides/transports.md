# Transport Adapter

## 当前配置驱动 Runtime

v1alpha3 Host 启动器只支持 Local Channel/RPC。队列容量由 runtime.yaml 配置，不受
MCU 全局编译期容量限制；回调在接收实例的执行器上运行，异步任务拥有编码数据。
满队列返回背压，广播、超时和关闭语义见 [runtime-v3](runtime-v3.md)。

以下 CAN/USB 是已有 Adapter 与 v1alpha2 生成 Node 的回归路径，**尚未成为新启动器
可选择的后端**。旧测试通过不等于 v1alpha3 跨平台部署已经接通。

## 旧 v1alpha2 Adapter 回归实现（不可由 `aster run` 选择）

旧编译器从 Application Graph 取得逻辑 Route；新部署契约将使用 Topic/RPC 名称、
类型与节点，不再使用 Module Port-to-Port 连接。物理协议不能泄漏进业务接口。

旧 Local

: 有界的进程内 Channel/RPC 分发。Registry 在使用前封闭，并记录容量与回调失败。

CAN / SocketCAN

: 使用稳定 Route ID、分片/重组、可靠 RPC acknowledgement/retry、deadline、peer
  restart 检测、背压和统计。握手只验证已知 Deployment ID、Node identity 与 Schema
  Hash，不执行开放式发现。v0.2 使用 classic CAN：控制面保留 Route ID 1--7，自动
  Application ID 从 8 开始；Resolver 根据最大编码长度计算分片和链路预算。

  Zephyr 的 `CanDeviceAdapter` 在驱动 callback 中只复制 frame 与时间戳到有界
  `k_msgq`，协议分发在 Executor 线程执行。Linux endpoint 使用 Hardware Profile
  指定的 `can0` 或 `vcan0`。生成的 Transport Module 先于业务 Module 启动、后于它们
  停止，并拥有 handshake、heartbeat、同步、重试和重组生命周期。

USB CDC ACM

: Zephyr CDC ACM 与 Linux TTY 共享 COBS 和 CRC32C framing。产品必须显式配置
  VID/PID；示例值不能作为量产分配。Zephyr endpoint 映射到 Devicetree 节点，Linux
  endpoint 映射到绝对 TTY 路径。v0.2 的 USB 支持 Channel，Resolver 会拒绝尚未实现
  的 USB RPC。

UDP、Zenoh、ROS 2、AimRT、gRPC 与 MQTT 留待后续阶段。Transport Interface 或
CorePlugin 查询入口不等于可用的插件系统：生产生命周期与后端注册尚未接通，不能
把这些名字填进新启动器就期待运行。

旧握手还使用完整 Deployment Hash；通信契约身份与业务参数、产物完整性的拆分
尚未完成，见[实施审计](../development/convergence-audit.md)。
