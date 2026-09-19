# Bounded Protobuf profile

AsterCtrl uses `.proto` as the schema language and the Protobuf wire format,
but its production Runtime does not link Google Protobuf. `aster codegen`
with `--proto` or `--descriptor` consumes schema input plus explicit bounds
and emits fixed-size C++ values, encoders, decoders, maximum wire sizes and a
Schema Hash. Generated headers carry schema-specific constants and share one
guarded bounded-wire implementation, so a translation unit can include types
from multiple schemas safely.

The v1alpha3 Package Manifest describes Module exports only. Generate message
types separately and consume the resulting headers in the application's CMake build:

```console
aster codegen --proto proto/state.proto --bounds proto/bounds.yaml --output build/state.pb.hpp
```

The `spec.exports.protos` and `spec.protobuf` metadata and automatic node-build
integration belong to the retained v1alpha2 deployment compiler. They are not
valid fields of the new Package Manifest.

The Schema Hash is derived from descriptors and bounds, so changing a bound
changes the hash. Communication compatibility must be kept separate from
business configuration and artifact integrity. The old deployment handshake
still uses a whole-deployment hash; that identity separation is not implemented yet.

The profile rejects maps, `google.protobuf.Any`, recursive message graphs,
unbounded strings, unbounded bytes and unbounded repeated fields. Encoded values
have a build-time upper bound; executor and transport queue budgets are separate
configuration contracts. Unknown fields are
skipped for forward compatibility; truncated varints, invalid wire types,
oversized length-delimited values and capacity overflow return a protocol or
capacity error.

Google Protobuf remains a Host test dependency only. Golden-vector tests encode
the same values through the generated TypeSupport and the official runtime to
prove wire compatibility.

## RPC services

Unary `service` methods use the same descriptor set and Schema Hash as their
request and response messages. For this schema:

```proto
service Calculator {
  rpc Add(AddRequest) returns (AddResponse);
}
```

`aster codegen` emits the method tag
`rpc::v1::Calculator::Add` and its
`aster::ServiceTypeSupport` specialization. The specialization exposes the
request and response types, the Protobuf service full name
(`rpc.v1.Calculator`), the method full name
(`rpc.v1.Calculator.Add`), and an `aster::ServiceDescriptor`. The generated
tag can be used with `aster::RpcClient` and `aster::RpcServer`; application code
does not maintain a parallel RPC type registry. Backend implementations such
as `LocalRpc` belong to the Runtime SDK, not the Module SDK.

Client-streaming, server-streaming and bidirectional-streaming methods are
outside the v0.2 bounded profile and fail code generation. A bounded streaming
Interface would need explicit limits for in-flight items, flow control and
cancellation, so unary RPC remains the only first-class RPC shape in v0.2.

## Why not use generated Google Protobuf classes on the MCU?

The schema language and interoperable wire format are useful on both Linux and
MCUs. The general-purpose runtime is not a good default for deterministic hot
paths because its ordinary containers and allocation behaviour are not bounded
by the schema. Keeping TypeSupport as an Interface also leaves room for a future
fixed-layout or CBOR Implementation without changing Module communication APIs.
