# 测试策略

测试与门禁按契约组织。下列内容是覆盖范围和验收要求，不代表当前版本已在所有
环境执行通过；实际证据见[实施审计](../development/convergence-audit.md)。

- Runtime：Registry 封闭、生命周期回滚、Channel、RPC、Parameter、CoreRef 和 C ABI。
- CLI/Graph：Runtime 配置校验、生成 Package/独立安装 SDK、旧图负向规则、
  逐字节确定性的 Lock 与生成输入。
- Protobuf：官方 runtime golden vector、未知字段、截断、非法 wire type、bounds 与 fuzz。
- Transport 回归：Local、旧 CAN/SocketCAN 适配器、可靠确认/重试、SocketCAN `vcan`、
  USB COBS/CRC 和 pseudo-TTY。它们不等于新 Launcher 已接通跨节点后端。
- Zephyr 门禁：工作流会尝试同一 pub/sub Module 的 `native_sim`/QEMU、两块官方板的
  编译链接和尺寸检查；请以对应 GitHub Actions 运行记录为证据。v1alpha3 静态部署
  本身仍未完成，实板 smoke 也不由普通 CI 代替。

实板 smoke 属于发布证据，不是普通 CI 仿真。v0.2.0 要求 `dev_c` 与 `mc02` 的
console、clock、CAN loopback、UART、SPI 和 watchdog 记录；USB 枚举作为独立的
未完成硬件验证项标注。
