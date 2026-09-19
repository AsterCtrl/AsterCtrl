# CLI 参考

唯一对外命令是 `aster`，Python import package 是 `aster_cli`。

## 当前命令边界

| 命令 | 当前状态 |
| --- | --- |
| `init`、`doctor`、`validate`、`run` | 当前 Linux 流程；`run` 的启动器只接通 Local Channel/RPC |
| `codegen --package`、`codegen --proto` | 当前 Package 入口和 bounded Protobuf 生成 |
| `graph`、`resolve`、`build` | 旧 v1alpha2 部署回归；不能消费 v1alpha3 `runtime.yaml`/`deployment.yaml` |
| `deploy plan/apply/status` | 旧 v1alpha2 Bundle/Inventory 运维回归；不启动服务或烧录固件 |

`aster init`

: 创建最小 Linux Module Package 和 runtime.yaml，不生成启动器或 Port 图。

`aster doctor`

: 检查 Python、uv、CMake、Ninja、protoc、west 与编译器。

旧 `aster package` 依赖管理命令已移除。依赖交给 CMake、west 和 uv；
Package Manifest 只描述框架导出元数据。

`aster validate runtime.yaml [--runtime PATH]`

: 使用原生 Runtime 解析器检查配置，不加载 Package、不运行 Module。
  需要 PATH 中已有 `aster_runtime` 或显式指定路径。其他文档类型使用各自 Schema。
  此命令不会检查 C++ 的实际注册结果。

`aster graph` / `aster resolve` / `aster build`

: 保留的 v1alpha2 图编译、Node 生成和 CMake/west 构建路径，会输出迁移警告。
  尚不能消费 v1alpha3 runtime 配置；`graph` 也不是运行时通信关系查询。

`aster run --config runtime.yaml`

: 直接启动通用 Host Runtime，不要求 Deployment Lock 或 `--execute`。
  默认在 PATH 查找 `aster_runtime`；可用 `--runtime PATH` 指定。
  `--check` 初始化并封闭注册表后退出；`--duration-ms N` 用于限时自检。

`init`、Package 入口生成与新 Runtime 使用 v1alpha3；部署编译仍处于 v1alpha2
迁移阶段。详见 [迁移说明](../guides/runtime-v3.md)。

`aster codegen --package package.yaml --output build/package`

: 生成 Linux C ABI 入口及 CMake 源文件列表。一般由 `aster_add_package` 调用，
  不执行 Package 自带的 Python。

`aster deploy plan|apply|status`

: 验证 Bundle，通过 Local/SSH Adapter 分阶段部署，并查询 Deployment ID 和 digest。
  远程执行及所有变更都要求 `--execute`。
  当前仍是 v1alpha2 工具，只切换文件，不启动 systemd 服务、不烧录 MCU 固件。
  详见[部署说明](../guides/deployment.md)。

`aster codegen --proto ...` / `aster codegen --descriptor ...`

: 从源文件或 descriptor set 与 bounds 生成有界 Protobuf 类型。

使用 `aster <command> --help` 查看完整参数。
