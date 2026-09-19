# 发布检查表

alpha 发布要求：确定性 Lock/生成物、GCC/Clang Host 测试、sanitizer、`native_sim`、
QEMU、两块板 link build、双语文档、许可证清单、SBOM、checksum 与回滚说明。发布
SBOM 必须逐项记录下载资产的字节数和 SHA-256。
资产清单不等于依赖 SBOM；FetchContent 引入的 yaml-cpp 与传递依赖是否被完整收录，
仍需在发布前审计。

最终 `v0.2.0` 还要求带日期的 `dev_c` 与 `mc02` 实板证据，覆盖 console、clock、
CAN loopback、UART、SPI 和 watchdog。USB 枚举可在 Release Notes 中明确标为尚未
验证。

本检查表本身不自动发布版本或操作远端；发布工作流另行执行。已有 legacy 历史归档
保持不变。早期计划中“发布后再归档”的顺序属于历史决策，不能作为现在重复归档或
删除仓库的指令。

实板记录必须绑定 board、operator、UTC 时间、仓库 revision、firmware hash 和完整
serial log hash；CI link build 或手写 JSON 不能代替实测证据。
