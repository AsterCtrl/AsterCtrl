# 算法与实物、仿真环境

仿真引擎不属于 AsterCtrl 核心。目标是算法 Module 只处理消息和 RPC，
实物端由 Driver Module 采集/执行，仿真端由 Bridge Module 与外部仿真器通信。
改变提供这些数据的 Module 和通信配置，而不是在算法源码里判断平台或仿真环境。

当前 Linux 已支持通过 runtime.yaml 选择实例、参数、namespace/remap 和执行器，
并混合静态/动态 Module。它不要求 application.yaml，也不要求通用 Hardware Profile。
算法能否复用还取决于双方消息语义、时钟行为和容量契约一致，并非改一个文件名就自动完成。

**ROS Bridge、Linux IP Transport、MuJoCo/Isaac 接入、可配置 Clock 替换、
录制回放和 PIL 尚未交付。** 不把这些作为本轮已可运行的教程。

现有 examples/provider_swap 仅是旧 v1alpha2 Provider/Clock 替换回归，
不是新用户模型。v1alpha3 同一算法在 Linux 与 Zephyr native_sim 运行的完整示例
也仍在验收清单中。
