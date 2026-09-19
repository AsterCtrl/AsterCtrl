# 构建与 CI

Host 构建需要 Python 3.12、uv、CMake 3.28+、Ninja 和 C++20 编译器。Python 依赖
固定在 `uv.lock`，CMake 导出 `aster::` 命名空间 targets。标准 preset 包括
`host-debug`、`host-clang`、`host-asan` 和 `host-tsan`。

```console
uv sync --frozen --all-groups
cmake --preset host-clang
cmake --build --preset host-clang
ctest --preset host-clang --output-on-failure
```

Host 依赖只有在 commit 或 archive hash 已固定时才可使用 `FetchContent`；Zephyr、
HAL、module 与 board 统一由 `west.yml` 管理。Host 离线检查先准备 Python 依赖及
yaml-cpp 源码，再禁止网络完成构建；这不等于 Zephyr 离线构建已验证。

文档源码统一使用 MyST Markdown。Doxygen 仅从公共接口产生 XML，Breathe 再把 API
内容嵌入英文与中文 Sphinx 站点：

```console
cmake -E make_directory build/document/doxygen
doxygen document/doxygen/Doxyfile
uv run python -m sphinx -W --keep-going -b html \
  document/sphinx-en build/document/html
uv run python -m sphinx -W --keep-going -b html \
  document/sphinx-cn build/document/html/zh_CN
```

PR 工作流配置了 Clang 18 format/tidy、GCC/Clang、Host 测试、sanitizer、两种 Linux
架构、bounded Protobuf、图负向 fixture、确定性生成、双语文档、依赖审计、Zephyr
Twister 以及板级尺寸。

工作流存在不代表门禁已通过；本机实际执行证据与尚未执行的 Linux/Zephyr 门禁见
[实施审计](../development/convergence-audit.md)。
