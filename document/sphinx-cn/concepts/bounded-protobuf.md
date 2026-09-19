# 有界 Protobuf Profile

AsterCtrl 使用 `.proto` 作为 Schema 语言并保持 Protobuf wire format，但生产 Runtime
不链接 Google Protobuf。`aster codegen` 读取 proto 或 descriptor set 与显式 bounds，
生成固定容量 C++ 类型、编码器、解码器、最大 wire size 和 Schema Hash。

当前 v1alpha3 Package Manifest 只描述 Module 导出，不包含协议图元数据。
消息类型单独生成，再由应用的 CMake 构建使用：

```console
aster codegen --proto proto/state.proto --bounds proto/bounds.yaml --output build/state.pb.hpp
```

`spec.exports.protos`、`spec.protobuf` 与自动注入 Node 构建的逻辑属于旧 v1alpha2
部署编译器，不能直接放入新的 Package Manifest。

字符串、bytes 和 repeated 字段必须有上限；map、`google.protobuf.Any`、递归消息和
无界字段会被拒绝。未知字段可跳过；截断 varint、非法 wire type、长度越界和容量
溢出会返回协议或容量错误。Schema Hash 由 descriptor 与 bounds 推导，改变 bounds
会改变 Schema Hash。它不应与业务参数或产物摘要混为一谈；旧部署握手仍使用完整
Deployment Hash，通信兼容身份的拆分尚未完成。

Unary RPC 使用同一个 descriptor set。生成器为每个 method 产生 tag 和
`aster::ServiceTypeSupport`；streaming RPC 在 v0.2 中不支持，因为它还需要显式的
在途项数、流控和取消上限。

Host 测试使用官方 Google Protobuf 进行 golden-vector 对照。TypeSupport 本身保持
独立 Interface，以便未来增加 fixed-layout 或 CBOR Implementation，而不改变 Module
通信接口。
