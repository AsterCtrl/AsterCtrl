# AsterCtrl 0.2

<a href="../index.html">English</a>

AsterCtrl 面向 Linux 与 Zephyr 的机器人控制应用。Linux 已提供配置驱动的 Package
加载和 Module 运行；同源 Module 的 v1alpha3 Zephyr 静态部署路径仍在迁移。
普通 Linux 使用不再要求 Application 文件或 Port 连接表。

```{toctree}
:caption: 核心概念
:maxdepth: 2

concepts/architecture
concepts/graphs
concepts/packages
concepts/runtime
concepts/bounded-protobuf
```

```{toctree}
:caption: 教程
:maxdepth: 2

tutorials/getting-started
tutorials/first-module
tutorials/real-and-sim
```

```{toctree}
:caption: 平台与运维
:maxdepth: 2

guides/build-and-ci
guides/linux
guides/runtime-v3
guides/zephyr
guides/transports
guides/plugins
guides/deployment
guides/debugging
```

```{toctree}
:caption: 项目
:maxdepth: 2

development/index
reference/cli
reference/testing
reference/versioning
reference/release-checklist
```

```{toctree}
:caption: API
:maxdepth: 2

api/index
```
