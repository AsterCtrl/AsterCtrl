# Bounded Protobuf profile

AsterCtrl uses `.proto` as the schema language and the Protobuf wire format,
but its production Runtime does not link Google Protobuf. `aster codegen`
with `--proto` or `--descriptor` consumes schema input plus explicit bounds
and emits fixed-size C++ values, encoders, decoders, maximum wire sizes and a
Schema Hash. Generated headers carry schema-specific constants and share one
guarded bounded-wire implementation, so a translation unit can include types
from multiple schemas safely.

A Package connects that protocol contract to graph resolution instead of
copying generated hashes into every Module Manifest:

```yaml
spec:
  exports:
    protos: [../../proto/state.proto]
  protobuf:
    bounds: ../../proto/bounds.yaml
    includes: [../../proto]
```

The resolver compiles those sources to a canonical descriptor set and applies
the same bounds analysis used by code generation. It records
`schema_hash_source: descriptor_bounds` and `max_encoded_size` in the
Deployment Lock. A port's optional `schema_hash` is only a checked assertion.
Changing a bound therefore changes the Schema Hash, Application Hash and
Deployment ID. A connection may reserve a larger `max_size` for framing, but
may not reserve less than the derived encoded maximum; omitting it selects the
derived maximum exactly.

Deployment-oriented `aster codegen <workspace> <deployment> <output>` also
performs this generation for every Package-exported schema. It writes the
shared headers under `<output>/types` and adds that directory to each emitted
Linux or Zephyr node build. Applications therefore use one graph compilation
step; they do not need a second hand-written Protobuf generation command.

The profile rejects maps, `google.protobuf.Any`, recursive message graphs,
unbounded strings, unbounded bytes and unbounded repeated fields. Every queue
and encoded value therefore has a build-time upper bound. Unknown fields are
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
tag can be passed directly to `aster::RpcClient`, `aster::RpcServer` and
`aster::LocalRpc`; application code does not maintain a parallel RPC type
registry.

Client-streaming, server-streaming and bidirectional-streaming methods are
outside the v0.2 bounded profile and fail code generation. A bounded streaming
Interface would need explicit limits for in-flight items, flow control and
cancellation, so unary RPC remains the only first-class RPC shape in v0.2.

## Why not use generated Google Protobuf classes on the MCU?

The schema language and interoperable wire format are useful on both Linux and
MCUs. The general-purpose runtime is not a good default for deterministic hot
paths because its ordinary containers and allocation behaviour are not bounded
by the schema. Keeping TypeSupport as an Interface also leaves room for a future
fixed-layout or CBOR Implementation without changing Module ports.
