# 部署与产物安装

## 与本地运行分开

普通 Linux 使用 runtime.yaml 和 aster run，不依赖 Bundle 或 Deployment Lock。
可选 v1alpha3 deployment.yaml 将负责多节点/Zephyr 放置和通信约束，但编译器尚未完成，
本文不提供假装可执行的新部署 YAML。

## 现有安装工具：旧输入的回归路径

现有 deploy plan/apply/status 仍读取 v1alpha2 Deployment、Inventory 和 Bundle。
Bundle 记录文件摘要、尺寸及旧 Deployment ID；plan 校验后只输出动作。
默认生成器打包的是生成输入，构建产物描述尚未成为独立的部署输入。

`deploy apply --execute` 才执行变更：Local/SSH Adapter 先暂存并校验文件，再
切换 current 链接，保留 previous 与 .aster-deploy-state.yaml 供检查和回滚。
它**不调用 systemctl、不自动启动/重启业务进程，也不刷写 MCU**。
serial/debug-probe 目标当前会被拒绝。部署切换不等于机器人已经运行或健康。

Inventory 不保存凭据；SSH 使用操作者的 agent/config，远程执行要求显式 --execute。
SSH 只传送文件，不是应用数据 Transport。

旧回归示例命令如下，不能用于新 runtime.yaml：

```sh
aster codegen workspace.yaml deployment.yaml build/generated
aster deploy plan deployment.yaml inventory.yaml build/generated
aster deploy apply deployment.yaml inventory.yaml build/generated --execute
aster deploy status inventory.yaml
```

## 待完成的连接

新部署编译器需要保持业务配置不随放置变化，生成节点输入并校对实际注册。
通信兼容身份应由通信契约决定，不能随日志/业务参数变化；产物完整性摘要独立计算。
现有握手仍使用旧完整 Deployment ID，这项分离尚未实现。

构建完成后的产物描述、systemd 配置、安装和回滚需要贯通测试。
安装目录切换与进程生命周期是两个不同操作，文档不能把前者写成后者已完成。
