# 调试

## 新 Linux 路径

先用 aster doctor 检查环境，再按检查深度选择：

```sh
aster validate runtime.yaml
aster run --config runtime.yaml --check
aster run --config runtime.yaml
```

validate 复用原生解析器，只检查配置；不加载 Package 或执行 Module。
run --check 会加载、初始化、封闭注册并关闭，因此 Initialize/Shutdown 的应用行为
确实会执行。它检查本节点实际注册，不会离线推导任意 C++ 或完成跨节点握手。
Runtime 不在 PATH 时，两种命令都可传 --runtime PATH。

日志包含实例身份和执行器上下文。嵌入者可检查 NodeRuntime 的 state、diagnostic
和 VisitGraph；后者目前只列出 Module 实例，并非完整通信图或分布式监控。
没有完整健康监控端点；不要在问题报告中假定它存在。

内存/并发问题先在 Host 使用 ASan/UBSan/TSan 复现。vcan 需要 Linux；
pseudo-TTY 可检查 USB framing，但不能证明实际 USB 枚举或 CAN 电气链路正常。

## 旧部署和 Zephyr 回归

旧 aster graph/resolve 与 deployment.lock.yaml 只适用于保留的 v1alpha2 路径，
不是新 Linux 调试的前置条件。Zephyr 后续需检查静态配置、overlay、链接尺寸、
ISR handoff 和实板 smoke；当前新部署路径尚未验收。

报告至少提供版本、runtime.yaml（移除敏感值）、平台、复现命令及首个错误；
只有旧部署问题才附旧 Lock。硬件问题另附板卡、固件与串口日志摘要。
