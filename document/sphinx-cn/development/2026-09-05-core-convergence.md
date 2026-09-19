# 核心收敛：实施记录

状态：进行中。这不是版本发布公告。

## 已实现与已检查

- 先复现，再修复动态 Package 的实例 CoreRef 被默认值覆盖的问题。
- 公共服务引用改为单指针 C ABI facade；Backend 分派与固定容量 Local
  实现移入 Runtime SDK。
- Runtime、平台与 Transport 头文件移出 Module Interface 目录，新增独立的
  `aster::runtime_interface` CMake Target。
- Linux Module 初始化和启动异常转换为错误状态，执行生命周期回滚；
  Shutdown 保持不抛异常，嵌入式构建保持禁用异常。
- ABI 2 引入带类型标签的配置和参数值，不再把任意 C++ 对象内存当作配置。
  参数读取复制到调用者提供的存储；不可变配置的字符串在关闭前保持有效。
- 构建选项、Interface Target、安装和测试移入 `cmake/`。
- 修正发布工作流中的双语文档门禁名称。
- CoreRef 提供独立实例名；普通服务调用自动取得 Runtime 执行上下文。
- Clock 读取失败显式返回状态，并迁移了 CAN 计时调用。
- `aster run --config` 接通通用 `aster_runtime`，无需 Deployment Lock。
- v1alpha3 Host 解析器接通具名串行/线程池执行器、类型化配置/参数和实例日志。
- Local Channel 按实例 namespace/remap 注册，并以拥有消息的异步任务交付。
- RPC 区分服务实例和方法，预留客户端完成队列，支持超时和关闭取消。
- 动态 Package 生成目标只链接 Module/Package Interface，不链接 Runtime。
- yaml-cpp 归档摘要固定，支持预置源码，安装包携带其许可证。
- 默认 init 模板改为 v1alpha3 Linux 项目，`aster_add_package` 从纯导出元数据
  自动生成入口；移除旧依赖管理器及发布工作流中的调用。
- 已验证安装后的独立 SDK 可以构建该模板，仅修改 YAML 不改变 Package 二进制。
  YAML 重复键现在会被明确拒绝。

关键回归先复现失败再修复。本机 macOS Host 在 AppleClang 和 LLVM 18 下通过
36 项 C++ 测试；129 项 Python 测试、ASan/UBSan、TSan、独立安装消费者、禁止依赖、
固定版本、拼写检查与双语 Doxygen/Sphinx 构建通过。bounded protobuf 解码器通过
带 Sanitizer 的 10,000 次 libFuzzer 运行。

预置 yaml-cpp 后，在 macOS sandbox 禁止网络的情况下完成配置和构建，并通过连接
探针确认网络确实被拒绝。Linux CI 已加入 network namespace 离线构建，尚未远端执行。
新 Host 管理器、解析器、Runtime 与执行上下文通过定向 clang-tidy 18 检查。

Package 代码生成使用隔离的 Python 导入；回归测试在应用目录放入同名 Python 文件，
确认它不能遮蔽已安装的 CLI，也不会被代码生成过程执行。

集成测试使用两个同类型动态实例与一个静态接收实例；仅改变 YAML 参数就改变
结果。加载库在关闭后不再被持有；初始化/启动失败时，已排队回调不执行。

## 尚待验收

- CorePlugin 生命周期及生产启动流程中的 Backend 注册。
- v1alpha3 可选部署编译器与 Zephyr 静态生成。
- 新启动器的 CAN/USB 接线、通信契约身份与注册核对。
- 去除泛化 Hardware/Capability/Provider 图编排。
- v1alpha3 跨平台示例与官网迁移；双语默认教程已收敛，不能用旧示例替代新路径验收。
- Linux GCC、实际隔离网络的 CI 和板卡编译验证。
- 双板 smoke 与跨节点实物测试；目前不宣称硬件验收完成。

## 后续检查：文档一致性与遗漏修复

移除当前指南中的强制双图用法，明确旧 Graph/Provider/Transport 代码仅为迁移回归。
完整逐项核对见[实施审计](convergence-audit.md)。历史日志不改写为当前设计。

本次按既有 Interface 做小范围修正，并先复现失败再实现：

- Runtime 配置校验走原生解析器，`--validate-config` 不加载 Package；
  `run --check` 才执行初始化/封闭/关闭。
- systemd unit 改用 CMake 配置的安装路径和 `aster_runtime --config`；集成测试
  运行其命令，未执行真实 systemd 服务管理。
- 动态 Package 工厂返回错误类型时拒绝并销毁对象，不加入实例集合。
- 旧 graph/resolve/build 输出迁移警告；不引入第二个 YAML Runtime 解析器或新图抽象。

更新后本机 macOS 验证：AppleClang 与 LLVM 18 各 37 项 C++ 测试通过，
ASan/UBSan 与 TSan 各 37 项通过；Python 130 项通过，包含独立安装 SDK 消费者。
全源码 clang-format 18、定向 clang-tidy 18、Ruff、禁止依赖/固定版本、拼写与
双语 Doxygen/Sphinx 构建通过。仍未验证 Linux `vcan`、systemd、Zephyr 或实板。

保留既有未提交修改；不操作遗留仓库和机器人业务代码；不自动提交、打标签、
发布版本或修改远端。
