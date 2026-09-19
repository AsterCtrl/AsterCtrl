# 快速开始

安装 Python 3.12、uv、CMake 3.28+、Ninja 和 `protoc`，然后运行：

```console
uv sync --all-groups
uv run aster doctor
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

创建、构建并运行最小 Linux Module Package：

```console
uv run aster init hello-aster
uv run cmake -S hello-aster -B hello-aster/build -G Ninja \
  -DASTERCTRL_SOURCE_DIR="$PWD"
uv run cmake --build hello-aster/build
uv run aster run --runtime hello-aster/build/asterctrl/aster_runtime \
  --config hello-aster/build/runtime.yaml
```

生成的 YAML 与 Module 源码都是普通项目输入，应该纳入版本控制。开发时可用
`ASTERCTRL_SOURCE_DIR` 指向源码树；安装后的 SDK 则通过 `find_package(AsterCtrl)`
发现。

模板只包含 Module 头文件/源码、Package 导出元数据、CMake 与 runtime.yaml。
当前模型没有自写 main、独立 Application 图或 Deployment Lock 文件。
`aster_add_package(demo MANIFEST package.yaml)` 在构建目录自动生成 C ABI 入口，
Package 只链接 Module/Package Interface。修改 runtime.yaml 后重新运行 CMake
（或触发重新配置的 build）即可更新启动配置，不会重新编译 Module。Ctrl-C 停止运行。

使用安装后的 SDK 时，将 ASTERCTRL_SOURCE_DIR 换成 CMAKE_PREFIX_PATH，并将
SDK 的 bin 加入 PATH。CMake 配置时需要启用 Python 3.12 的 aster_cli 环境。
Zephyr 与跨节点尚待完成的工作见 [迁移状态](../guides/runtime-v3.md)。
