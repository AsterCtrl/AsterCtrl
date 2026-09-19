# Package、Module 类型与实例

Package 是可构建、导出的 Module 集合，不是自建依赖管理器。

v1alpha3 package.yaml 描述 Package 身份、版本、许可证，以及每种 Module 的
导出类型名、C++ 类、头文件、源码和平台支持。它不强制声明 Port，也不维护依赖锁。
CMake/west/uv 分别负责 Host、Zephyr 和 Python 依赖。

- **Module 类型**：例如 demo.Hello，对应一份类实现。
- **Module 实例**：例如 left、right，在 runtime.yaml 中分别配置；同一类型可以创建多次。
- **Package**：例如 demo，由 CMake 构建成 Linux 动态库，供 Runtime 创建实例。

`aster_add_package(demo MANIFEST package.yaml)` 自动生成注册数组与 C ABI
catalog/create/destroy 入口。业务代码只实现 ModuleBase，不手写导出宏或启动器。
默认生成目标只链接 Module/Package Interface，C++20 由 Interface Target 传递。

静态 Module 通过 NodeRuntime 的 RegisterModule 注册，动态 Module 由 Package 创建；
二者使用相同 ModuleRef 生命周期。使用方式和完整 Manifest 见 [Package 指南](../guides/plugins.md)。

CorePlugin 与 Module Package 是不同扩展入口。当前 CorePlugin 只有加载和接口查询
基础，生产生命周期与后端注册还未接入新 Runtime。v1alpha3 Manifest 的 Zephyr
静态生成也尚未完成，不应把这两项写成可用功能。
