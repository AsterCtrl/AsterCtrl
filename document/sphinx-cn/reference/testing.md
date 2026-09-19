# 测试策略

测试与门禁按契约组织。下列内容是覆盖范围和验收要求，不代表当前版本已在所有
环境执行通过；实际证据见[实施审计](../development/convergence-audit.md)。

- Runtime：Registry 封闭、生命周期回滚、Channel、RPC、Parameter、CoreRef 和 C ABI。
- CLI/Graph：Runtime 配置校验、生成 Package/独立安装 SDK、旧图负向规则、
  逐字节确定性的 Lock 与生成输入。
- Protobuf：官方 runtime golden vector、未知字段、截断、非法 wire type、bounds 与 fuzz。
- Transport：Local、CAN 丢包/乱序/重启、可靠确认/重试、SocketCAN `vcan` 生命周期、
  USB COBS/CRC 和 pseudo-TTY。
- Zephyr：要求同一 pub/sub Module 在 `native_sim` 和 QEMU 执行，两块官方板编译
  链接并检查尺寸。本轮 macOS 环境尚未执行这些门禁，v1alpha3 静态部署也尚未完成。

实板 smoke 属于发布证据，不是普通 CI 仿真。v0.2.0 要求 `dev_c` 与 `mc02` 的
console、clock、CAN loopback、UART、SPI 和 watchdog 记录；USB 枚举作为独立的
未完成硬件验证项标注。
