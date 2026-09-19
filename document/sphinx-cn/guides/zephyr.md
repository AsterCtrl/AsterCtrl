# Zephyr Runtime：迁移边界

Zephyr 是 MCU 的操作系统，AsterCtrl 在其上提供与 Linux 一致的 Module Interface。
可移植的是 C++ 源码，不是动态库。目标是用同一份 Runtime/Package 配置，经可选部署
编译生成静态注册、资源表、Kconfig fragment 和 Devicetree overlay。

**v1alpha3 这条部署路径尚未完成。** 当前 Zephyr 生成器仍消费 v1alpha2 的
Workspace/Application/Deployment/Hardware 输入；不能直接拿新 runtime.yaml 运行
aster resolve/build，也不能把旧流水线成功当成新配置的双平台验收。

## 已有实现与约束

现有平台代码包含生命周期、线程执行器、有界 k_msgq、固定内存和 ISR handoff，
以及 CAN Device/USB CDC ACM Adapter。Module 初始化时注册，启动前封闭；
异常和 RTTI 关闭，ISR 不直接执行算法或分配内存。

这些实现仍需完成新实例配置、资源预算和通信契约适配。旧参数路径并未接通完整的
可变 Parameter 服务；HardwareManager/Provider 仍是待清理的迁移实现。
公共 SDK 留有接口，不等于 MCU 平台已提供了该服务。

## 工具链与验收

版本依据仓库 west.yml 与 CI：Zephyr 4.4.0、SDK 1.0.1。
CI 配置了 native_sim、QEMU、dev_c/stm32f407xx 与 mc02/stm32h723xx；
配置存在不代表本轮已经执行或通过。当前本机尚无可用 Zephyr SDK，
不能宣称新的静态部署、尺寸预算和热路径无堆分配已验收。

板级仓库为 AsterCtrl/asterctrl-boards，名称不带机构前缀。
正式版仍需两块实板的 console、clock、CAN loopback、UART、SPI、watchdog 证据，
以及真实跨节点测试。编译产物、CI 配置和人工填写的通过声明不能替代实测日志。

新部署路径完成后，普通用户只维护配置，框架/Board/Driver 作者维护基础 Kconfig/DTS。
在此之前请将旧示例视为迁移回归，见 [实施审计](../development/convergence-audit.md)。
