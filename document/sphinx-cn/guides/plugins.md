# 插件

依赖交给 CMake、west 和 uv；旧 `aster package add/remove/list/lock` 已移除。
v1alpha3 Package Manifest 只负责导出类型、C++ 类、源码入口、平台和许可证，
不重复建设依赖解析或强制 Port 声明。

```yaml
api_version: aster.dev/v1alpha3
kind: Package
metadata: {name: demo, version: 0.1.0, license: Apache-2.0}
spec:
  modules:
    - type: demo.Hello
      class: demo::Hello
      header: src/hello.hpp
      sources: [src/hello.cpp]
      platforms: [linux, zephyr]
```

路径相对于 Manifest，且必须留在 Package 内。
`aster_add_package(demo MANIFEST package.yaml)` 自动生成动态入口；应用自己的依赖
直接用 `target_link_libraries` 添加。生成的 Target 只需要 Module/Package Interface，
不链接 Runtime，不执行 Package 自带的 Python。当前 helper 选择 Linux 导出项；
v1alpha3 Zephyr 静态入口仍在迁移。

Module Package

: 提供业务 Module。Linux 通过 C ABI 动态创建，`aster codegen` 自动生成 catalog、
  create 和 destroy 入口。

Core Plugin

: 用于提供平台或 Transport Implementation，但生产生命周期和后端注册还没有接入
  新配置驱动 Runtime。只有动态库加载测试不能算完成该功能。

Runtime 在 Start 前封闭注册。跨节点通信契约和 Zephyr 静态资源属于可选部署编译，
不是普通 Linux Package 构建的前置条件。详见 [迁移状态](runtime-v3.md)。
