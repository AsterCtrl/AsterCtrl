# 版本与兼容性

发布遵循 Semantic Versioning。C ABI 根结构携带 ABI version 与 `struct_size`；YAML
携带 `api_version` 与 `kind`；消息兼容性由规范化 descriptor hash 标识；Deployment
Lock 记录类型、Route 和 artifact digest。

`v1alpha2` 与 v0.1 实验 Schema 有意不兼容。项目不提供 `asterctl` 或 `aster_tools`
兼容层。
