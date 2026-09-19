:orphan:

# Link-U-OS 对 AsterCtrl 的参考评估

审计日期：2026-09-18。

这份记录只依据 Link-U-OS 公开仓库和其固定的子仓库源码。Link-U-OS 的组织仓库
在当前 revision `64ac7b08dd609dfb463310945856419be470315a` 中，把 AimRT、协议、
通信扩展、健康监控、进程管理、集成构建、仿真和 RL 部署作为多个 pinned submodule
组合起来；它不是一个提供 Zephyr/MCU 内核的仓库。

## 先确认它实际解决了什么

Link-U-OS README 将自己的南向能力描述为硬件抽象和分布式通信，将北向能力描述为
智能体服务，并将开发、仿真、部署和数据记录放在同一生态中。核心实现依赖 AimRT，
而公开的 RL 部署工程使用 AimRT 的 iceoryx 或 ROS 2 后端。其 `integration` 仓库
负责 Bazel 依赖、版本一致性、自定义规则和 x86_64/Orin aarch64 toolchain；这证明
了 Linux/SOC 级工程集成能力，不证明 MCU 静态运行时能力。

公开的 `rl_deploy` 代码把仿真中的 MuJoCo module 与实物中的 EtherCAT hardware
system 放在同一组 Topic 契约之后，控制器只看关节状态、IMU 和关节命令。闭链机构
的坐标转换位于 hardware system 一侧，而不是 RL 控制器里。这是 AsterCtrl 最有价值
的设计参考：替换的是硬件 Adapter，算法 Module 的通信契约不变。

## 可以直接借鉴的设计

### 1. 增加一个真正的集成层

`integration` 仓库把工具链、第三方依赖、平台选择和最终打包集中管理；`build_package.sh`
还把仓库 revision、Protobuf/ROS 版本和产品版本写入产物 metadata。AsterCtrl 不需要
复制 Bazel，也不应因此拆出更多运行时仓库，但可以在现有 `cmake/`、`west.yml` 和
`aster` 中补一个等价的职责层：

- 统一维护 Host、Zephyr、`native_sim`、`dev_c`、`mc02` 的 toolchain/profile。
- 生成 `build-manifest.json`，记录 AsterCtrl revision、Package revision、协议
  Schema Hash、编译器、Zephyr revision 和产物 digest。
- 将“源码构建”“Node 固件/可执行文件”“部署 Bundle”分成三个明确产物阶段。
- 所有外部依赖使用固定 revision 或 digest；不采用 Link-U-OS 集成脚本中可被
  `source_branch` 覆盖的默认浮动依赖。

### 2. 把机器人协议作为可复用的契约层

`aimrt_protocol` 单独维护 Header、Timestamp、ControlSource、关节状态/命令和健康
协议。AsterCtrl 也应形成稳定的协议目录或协议 Package，而不是把每个示例的 message
定义散落在 Package 内。建议首批提供：

- `aster.header.v1`：sequence、source timestamp、frame/domain 和来源标识；
- `aster.health.v1`：heartbeat、状态、错误码和能力摘要；
- `aster.control.joint.v1`：有界关节状态与关节命令；
- `aster.rpc.v1`：请求关联 ID、结果码、时间戳和取消/超时语义。

这些字段应遵守 Aster bounded profile：字符串和 repeated 都有上限，不能照搬 ROS
消息中无界的 string/repeated。通用 Header 只承载跨模块真正需要的元数据，业务字段
仍由具体 message 定义，避免制造一个所有消息都必须携带的巨大 Envelope。

### 3. 把“仿真/实物切换”落在硬件 Adapter seam

Link-U-OS 的 `rl_deploy` 用同一组状态/命令 Topic 连接 MuJoCo 和实际 hardware
system。AsterCtrl 应把这一点做成官方示例和验收标准：

```text
portable algorithm Module
          │ Topic/RPC contract
          ├── simulated hardware Adapter
          └── real hardware Adapter
```

闭链运动学、驱动方向、限位、单位变换、传感器时间戳等放在 Adapter 内。算法只使用
有界 Channel/RPC，不依赖 `HardwareManager` 或某个 Board。`deployment.yaml` 选择
Adapter 和节点；业务 Module 源码不变。

这比当前 AsterCtrl 的 `provider_swap` 旧图例更适合长期保留。应新增一个基于
`runtime.yaml` 的 Linux sim/real 示例，并把旧 Provider/Capability 图例降为迁移测试，
替代示例通过后再删除旧图编译路径。

### 4. 引入“对齐帧”而不是只传最新消息

`aimrl_sdk` 的 C++ 实现使用 ring buffer、固定 tick、有限等待和时间戳 skew 检查，
输出 `complete`、`aligned`、缺失原因、延迟和 jitter 统计；不完整时保留上一帧完整
观测，避免把零填充数据误送进控制策略。

AsterCtrl 当前 `MessageInfo` 只有 sequence 和 source timestamp，Runtime 也没有
跨 Topic 观测帧同步模块。建议把对齐做成独立的深 Module：

- Linux 允许动态容量和 richer statistics；
- Zephyr 使用生成的固定容量 ring buffer；
- 核心接口只暴露 `Push(sample)`、`TryMakeFrame(tick)`、`Statistics()`；
- `complete/aligned/freshness/skew` 由该 Module 统一定义；
- 控制器明确使用“可用帧”门控，而不是自己拼接多个 Topic 的最新值。

它不应进入最小 Runtime 生命周期，也不应强迫所有普通 Module 使用。

### 5. 健康与进程管理应是独立平面

Link-U-OS 还拆出了 `aimrt_health_monitor` 和 `aimrt_process_manager`。后者负责
进程启动、停止、异常状态、重启和状态持久化；前者承载健康状态。AsterCtrl 当前有
Runtime 生命周期和部署目录切换，但没有清晰的健康协议与 Linux 进程监督层。

建议分两层实现：

- `aster.health`：Linux/Zephyr 都能发送的 bounded heartbeat、状态、错误码、序列和
  时间戳；
- Linux `aster supervisor`：进程退出检测、有限重启策略、日志/诊断路径和 Bundle
  状态；systemd 仍可作为外部进程管理器；
- Zephyr 只实现节点健康和 watchdog 状态，不复制 Linux 的 process manager。

健康平面不要侵入普通 Module 的 Channel/RPC 业务代码，也不要成为启动所有应用的
第二个 Runtime。

### 6. 建立面向产品的构建与部署证据

Link-U-OS 的产品包脚本为 x86_64 和 Orin aarch64 分别生成包，并写入 metadata。AsterCtrl
已有 `deployment.lock.yaml`、Bundle digest 和 current/previous 切换，下一步应补齐：

- 每个节点产物的 target triple、Board、工具链和 Schema Hash；
- 生成物清单与 SHA-256；
- 可回滚的 runtime/package/firmware 版本对应关系；
- 构建来源与测试证据的链接。

这些 metadata 不应成为业务通信身份；通信契约身份、产物完整性和部署版本必须分开。

## 不应照搬的部分

- Link-U-OS 的核心通信仍以 AimRT、iceoryx 和 ROS 2 为中心；AsterCtrl 可以提供
  Bridge，却不应让它们成为 Zephyr Runtime 依赖。
- AimRTe 的 `OnConfigure` 可以让应用在 YAML 中选择后端和执行器，但如果把任意深层
  YAML 覆盖直接暴露给 AsterCtrl，会重新制造隐式配置耦合。AsterCtrl 保留
  `Initialize(CoreRef)` 中的注册和类型化配置访问即可。
- Link-U-OS 的多仓库 submodule 组织适合产品团队，但与 AsterCtrl 当前“核心仓库 +
  boards 仓库 + 归档历史”的简洁目标冲突。借鉴职责分层，不复制仓库数量。
- 普通 ROS/Protobuf 消息包含无界 string/repeated 和 SOC 级消息体，不能直接作为 MCU
  协议。Aster bounded profile 和独立固定容量 codegen 仍是正确方向。
- Docker、AimStudio、OTA 和完整图形工具链可以作为 Linux/SOC 工具，但不应阻塞
  Zephyr 核心闭环；先保证命令行、可复现构建和明确 Bundle。

## 对 AsterCtrl 方向的调整建议

当前方向总体正确，但优先级应从“继续完善通用 Graph/Provider 能力”转成：

1. 先完成 CorePlugin/Transport seam，让 Local、CAN/SocketCAN 和 USB 都能由同一
   Runtime 生命周期管理。
2. 增加协议 Package、Header/Health/Joint 契约和 bounded 对齐帧 Module。
3. 实现同一算法 Module 对应 simulated hardware 与 real hardware Adapter 的
   Linux 示例和测试。
4. 完成 v1alpha3 Deployment 编译，生成各 Node 的静态/动态入口、toolchain 输入、
   资源预算和 build manifest。
5. 在 `native_sim` 通过后验证 `dev_c`、`mc02`，再清理 Provider/Capability/旧图代码。
6. 最后补 Linux supervisor、健康监控、产物回滚和官网工作流。

## 证据链接

- [Link-U-OS 主 README](https://github.com/Link-U-OS/Link-U-OS/blob/64ac7b08dd609dfb463310945856419be470315a/README.md)
- [Link-U-OS 子仓库清单](https://github.com/Link-U-OS/Link-U-OS/blob/64ac7b08dd609dfb463310945856419be470315a/.gitmodules)
- [integration README](https://github.com/Link-U-OS/integration/blob/3201d0d5a1d8f43107bff4e8086b3f277bbfbc98/README.md)
- [integration 产品打包脚本](https://github.com/Link-U-OS/integration/blob/3201d0d5a1d8f43107bff4e8086b3f277bbfbc98/tools/build_package.sh)
- [AimRTe README](https://github.com/Link-U-OS/aimrt_comm/blob/a295d5644700e6d2a186b8f3a053d2cf7966ffba/README.md)
- [rl_deploy README](https://github.com/Link-U-OS/rl_deploy/blob/2f6f2f4d2a7058e527e15882fa32539baafa13a5/README.md)
- [rl_deploy AimRL SDK README](https://github.com/Link-U-OS/rl_deploy/blob/2f6f2f4d2a7058e527e15882fa32539baafa13a5/aimrl_sdk/README.md)
- [AimRT 协议 Header](https://github.com/Link-U-OS/aimrt_protocol/blob/03263afbbe05b224e2525c70c7ec74afb119cecc/aimdk/protocol/common/header.proto)
- [AimRT 关节 Channel](https://github.com/Link-U-OS/aimrt_protocol/blob/03263afbbe05b224e2525c70c7ec74afb119cecc/aimdk/protocol/hal/joint/joint_channel.proto)
- [AimRT 关节类型](https://github.com/Link-U-OS/aimrt_protocol/blob/03263afbbe05b224e2525c70c7ec74afb119cecc/aimdk/protocol/common/joint.proto)
- [进程管理器 README](https://github.com/Link-U-OS/aimrt_process_manager/blob/f7611ad2eeaf56a20ef6ee3fb23c6c3e3c815955/README.md)
- [健康监控仓库](https://github.com/Link-U-OS/aimrt_health_monitor/tree/5c5515fde80d37e7302e119b7b900fac74555433)
