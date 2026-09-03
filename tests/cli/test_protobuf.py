from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest
from aster_cli.protobuf import (
    ProtobufProfileError,
    create_descriptor_set,
    generate_from_proto,
    inspect_from_proto,
)
from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

REPOSITORY = Path(__file__).resolve().parents[2]

PROTO = """syntax = "proto3";
package test.v1;
message Sample {
  string name = 1;
  repeated sint32 values = 2;
}
"""

WIRE_PROTO = """syntax = "proto3";
package wire.v1;

enum Mode {
  MODE_UNSPECIFIED = 0;
  MODE_ACTIVE = 2;
}

message Child {
  sint32 delta = 1;
  string label = 2;
}

message Everything {
  int32 i32 = 1;
  int64 i64 = 2;
  uint32 u32 = 3;
  uint64 u64 = 4;
  sint32 s32 = 5;
  sint64 s64 = 6;
  fixed32 fixed32_value = 7;
  fixed64 fixed64_value = 8;
  sfixed32 sfixed32_value = 9;
  sfixed64 sfixed64_value = 10;
  float float_value = 11;
  double double_value = 12;
  bool flag = 13;
  Mode mode = 14;
  string text = 15;
  bytes raw = 16;
  Child child = 17;
  optional uint32 maybe = 18;
  repeated sint32 packed_values = 19;
  repeated fixed32 unpacked_values = 20 [packed = false];
  repeated Child children = 21;
  repeated string labels = 22;
  optional string optional_text = 23;
  optional bytes optional_raw = 24;
}
"""

RPC_PROTO = """syntax = "proto3";
package rpc.v1;

message AddRequest {
  uint32 left = 1;
  uint32 right = 2;
}

message AddResponse {
  uint32 sum = 1;
}

message HealthRequest {}
message HealthResponse {
  bool ready = 1;
}

service Calculator {
  rpc Add(AddRequest) returns (AddResponse);
  rpc Health(HealthRequest) returns (HealthResponse);
}
"""


def _compile_and_run(tmp_path: Path, source_text: str, *, sanitize: bool = False) -> None:
    compiler = shutil.which("clang++" if sanitize else "c++")
    if compiler is None:
        pytest.skip("C++ compiler unavailable")
    source = tmp_path / "check.cpp"
    executable = tmp_path / "check"
    source.write_text(source_text, encoding="utf-8")
    command = [
        compiler,
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Wconversion",
        "-Werror",
        "-I",
        str(tmp_path),
        "-I",
        str(REPOSITORY / "include"),
    ]
    if sanitize:
        command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
    command.extend([str(source), "-o", str(executable)])
    subprocess.run(command, check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)


def _dynamic_message(
    descriptor_path: Path, full_name: str
) -> tuple[type, descriptor_pb2.FileDescriptorSet]:
    descriptor = descriptor_pb2.FileDescriptorSet.FromString(descriptor_path.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for file_descriptor in descriptor.file:
        pool.Add(file_descriptor)
    return message_factory.GetMessageClass(pool.FindMessageTypeByName(full_name)), descriptor


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_generates_fixed_capacity_cpp_and_hash(tmp_path: Path) -> None:
    proto = tmp_path / "sample.proto"
    proto.write_text(PROTO, encoding="utf-8")
    bounds = tmp_path / "bounds.yaml"
    bounds.write_text(
        "fields:\n  test.v1.Sample.name: {max_size: 16}\n  test.v1.Sample.values: {max_count: 4}\n",
        encoding="utf-8",
    )
    output = tmp_path / "bounded.pb.hpp"

    first = generate_from_proto([proto], output, bounds, [tmp_path])
    second = generate_from_proto([proto], output, bounds, [tmp_path])
    inspected = inspect_from_proto([proto], bounds, [tmp_path])

    assert first.schema_hash == second.schema_hash
    assert inspected.schema_hash == first.schema_hash
    assert inspected.message("test.v1.Sample") is not None
    assert inspected.message("test.v1.Sample").max_wire_size == 40
    assert first.messages == inspected.messages
    text = output.read_text(encoding="utf-8")
    assert "BoundedString<16> name" in text
    assert "BoundedVector<std::int32_t, 4> values" in text
    assert "static constexpr std::size_t kMaxWireSize" in text
    assert "struct TypeSupport<::test::v1::Sample>" in text
    assert "first 16 bytes of SHA-256" in text
    assert first.schema_hash in text
    compiler = shutil.which("c++")
    if compiler:
        descriptor_path = tmp_path / "sample.pb"
        create_descriptor_set([proto], descriptor_path, [tmp_path])
        descriptor = descriptor_pb2.FileDescriptorSet.FromString(descriptor_path.read_bytes())
        pool = descriptor_pool.DescriptorPool()
        for file_descriptor in descriptor.file:
            pool.Add(file_descriptor)
        sample_type = message_factory.GetMessageClass(pool.FindMessageTypeByName("test.v1.Sample"))
        reference = sample_type(name="hello", values=[-1, 0, 150, -(2**31)])
        golden = reference.SerializeToString(deterministic=True)
        golden_values = ", ".join(f"std::byte{{0x{byte:02x}}}" for byte in golden)
        hash_values = ", ".join(
            f"std::byte{{0x{first.schema_hash[index : index + 2]}}}" for index in range(0, 32, 2)
        )
        source = tmp_path / "check.cpp"
        source.write_text(
            "#include <array>\n"
            "#include <cassert>\n"
            "#include <cstddef>\n"
            "#include <limits>\n"
            "#include <span>\n"
            '#include "bounded.pb.hpp"\n'
            "int main() {\n"
            "  test::v1::Sample value;\n"
            '  assert(value.name.assign("hello"));\n'
            "  assert(value.values.push_back(-1));\n"
            "  assert(value.values.push_back(0));\n"
            "  assert(value.values.push_back(150));\n"
            "  assert(value.values.push_back(std::numeric_limits<std::int32_t>::min()));\n"
            f"  constexpr std::array golden{{{golden_values}}};\n"
            f"  constexpr std::array expected_hash{{{hash_values}}};\n"
            "  static_assert(test::v1::Sample::kMaxWireSize >= golden.size());\n"
            "  static_assert(aster::TypeSupport<test::v1::Sample>::descriptor()"
            ".schema_hash.bytes == expected_hash);\n"
            "  std::array<std::byte, test::v1::Sample::kMaxWireSize> encoded{};\n"
            "  std::size_t written{};\n"
            "  assert(aster::TypeSupport<test::v1::Sample>::Encode("
            "value, encoded, written) == aster::Status::kOk);\n"
            "  assert(written == golden.size());\n"
            "  for (std::size_t index = 0; index < written; ++index) "
            "assert(encoded[index] == golden[index]);\n"
            "  test::v1::Sample decoded;\n"
            "  assert(aster::TypeSupport<test::v1::Sample>::Decode("
            "golden, decoded) == aster::Status::kOk);\n"
            '  assert(decoded.name.view() == "hello");\n'
            "  assert(decoded.values.size == 4);\n"
            "  assert(decoded.values.values[0] == -1);\n"
            "  assert(decoded.values.values[2] == 150);\n"
            "  return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(tmp_path),
                "-I",
                str(REPOSITORY / "include"),
                str(source),
                "-o",
                str(tmp_path / "check"),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        subprocess.run([str(tmp_path / "check")], check=True)


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_independent_generated_headers_can_share_a_translation_unit(tmp_path: Path) -> None:
    alpha = tmp_path / "alpha.proto"
    beta = tmp_path / "beta.proto"
    alpha.write_text(
        'syntax = "proto3"; package alpha.v1; message Sample { uint32 value = 1; }\n',
        encoding="utf-8",
    )
    beta.write_text(
        'syntax = "proto3"; package beta.v1; message Sample { string label = 1; }\n',
        encoding="utf-8",
    )
    bounds = tmp_path / "beta.bounds.yaml"
    bounds.write_text("fields:\n  beta.v1.Sample.label: {max_size: 8}\n", encoding="utf-8")
    alpha_result = generate_from_proto([alpha], tmp_path / "alpha.pb.hpp", None, [tmp_path])
    beta_result = generate_from_proto([beta], tmp_path / "beta.pb.hpp", bounds, [tmp_path])

    assert alpha_result.schema_hash != beta_result.schema_hash
    _compile_and_run(
        tmp_path,
        '#include "alpha.pb.hpp"\n'
        '#include "beta.pb.hpp"\n'
        "int main() {\n"
        "  alpha::v1::Sample alpha_value;\n"
        "  beta::v1::Sample beta_value;\n"
        "  alpha_value.value = 42;\n"
        '  return beta_value.label.assign("ready") && alpha_value.value == 42 ? 0 : 1;\n'
        "}\n",
    )


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_generates_rpc_type_support_and_runs_local_rpc_contract(tmp_path: Path) -> None:
    proto = tmp_path / "calculator.proto"
    proto.write_text(RPC_PROTO, encoding="utf-8")
    output = tmp_path / "calculator.pb.hpp"

    first = generate_from_proto([proto], output, None, [tmp_path])
    first_bytes = output.read_bytes()
    second = generate_from_proto([proto], output, None, [tmp_path])
    inspected = inspect_from_proto([proto], None, [tmp_path])

    assert first.schema_hash == second.schema_hash
    add_contract = inspected.rpc_method("rpc.v1.Calculator.Add")
    assert add_contract is not None
    assert add_contract.request_type == "rpc.v1.AddRequest"
    assert add_contract.response_type == "rpc.v1.AddResponse"
    assert add_contract.request_max_wire_size == 12
    assert add_contract.response_max_wire_size == 6
    assert add_contract.max_wire_size == 12
    assert set(first.declarations) == {
        "rpc.v1.AddRequest",
        "rpc.v1.AddResponse",
        "rpc.v1.Calculator",
        "rpc.v1.HealthRequest",
        "rpc.v1.HealthResponse",
    }
    assert output.read_bytes() == first_bytes
    generated = output.read_text(encoding="utf-8")
    assert "struct Calculator final" in generated
    assert "struct Add final" in generated
    assert "struct Health final" in generated
    assert "ServiceTypeSupport<::rpc::v1::Calculator::Add>" in generated
    assert 'return "rpc.v1.Calculator";' in generated
    assert 'return "rpc.v1.Calculator.Add";' in generated
    hash_values = ", ".join(
        f"std::byte{{0x{first.schema_hash[index : index + 2]}}}" for index in range(0, 32, 2)
    )

    _compile_and_run(
        tmp_path,
        "#include <array>\n"
        "#include <cassert>\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <string_view>\n"
        "#include <type_traits>\n"
        '#include "calculator.pb.hpp"\n'
        "class InlineExecutor final : public aster::Executor {\n"
        " public:\n"
        '  std::string_view Name() const noexcept override { return "inline"; }\n'
        "  aster::Status TryPost(aster::WorkItem work,\n"
        "                        const aster::ExecutionContext&) noexcept override {\n"
        "    if (!work) return aster::Status::kInvalidArgument;\n"
        '    work.Run({"inline", aster::ExecutionKind::kThread, 10});\n'
        "    return aster::Status::kOk;\n"
        "  }\n"
        "  aster::Status TryPostAt(std::uint64_t, aster::WorkItem work,\n"
        "                          const aster::ExecutionContext& caller) noexcept override {\n"
        "    return TryPost(work, caller);\n"
        "  }\n"
        "};\n"
        "using Add = rpc::v1::Calculator::Add;\n"
        "using AddSupport = aster::ServiceTypeSupport<Add>;\n"
        "aster::Status HandleAdd(void*, const rpc::v1::AddRequest& request,\n"
        "                        rpc::v1::AddResponse& response,\n"
        "                        const aster::RpcCallInfo&,\n"
        "                        const aster::ExecutionContext&) noexcept {\n"
        "  response.sum = request.left + request.right;\n"
        "  return aster::Status::kOk;\n"
        "}\n"
        "struct Result { bool called{}; std::uint32_t sum{}; };\n"
        "void Complete(void* state, aster::Status status,\n"
        "              const rpc::v1::AddResponse& response,\n"
        "              const aster::RpcCallInfo&,\n"
        "              const aster::ExecutionContext&) noexcept {\n"
        "  auto& result = *static_cast<Result*>(state);\n"
        "  assert(status == aster::Status::kOk);\n"
        "  result.called = true;\n"
        "  result.sum = response.sum;\n"
        "}\n"
        "int main() {\n"
        "  static_assert(aster::ServiceType<Add>);\n"
        "  static_assert(aster::ServiceType<rpc::v1::Calculator::Health>);\n"
        "  static_assert(std::is_same_v<AddSupport::Request, rpc::v1::AddRequest>);\n"
        "  static_assert(std::is_same_v<AddSupport::Response, rpc::v1::AddResponse>);\n"
        '  static_assert(AddSupport::service_full_name() == "rpc.v1.Calculator");\n'
        '  static_assert(AddSupport::method_full_name() == "rpc.v1.Calculator.Add");\n'
        f"  constexpr std::array expected_hash{{{hash_values}}};\n"
        "  constexpr auto descriptor = AddSupport::descriptor();\n"
        '  static_assert(descriptor.name == "rpc.v1.Calculator.Add");\n'
        "  static_assert(descriptor.schema_hash.bytes == expected_hash);\n"
        '  static_assert(descriptor.request_type.name == "rpc.v1.AddRequest");\n'
        '  static_assert(descriptor.response_type.name == "rpc.v1.AddResponse");\n'
        "  InlineExecutor executor;\n"
        "  aster::LocalRpc<2, 32, 32, 1> backend{aster::ExecutorRef(executor)};\n"
        "  aster::RpcServer<Add> server;\n"
        "  aster::RpcClient<Add> client;\n"
        "  assert(server.Bind(aster::RpcRef(backend), HandleAdd, nullptr) ==\n"
        "         aster::Status::kOk);\n"
        "  assert(client.Bind(aster::RpcRef(backend)) == aster::Status::kOk);\n"
        "  assert(backend.Seal() == aster::Status::kOk);\n"
        "  rpc::v1::AddRequest request;\n"
        "  request.left = 20;\n"
        "  request.right = 22;\n"
        "  aster::RpcCompletion<Add> completion;\n"
        "  Result result;\n"
        '  const aster::ExecutionContext caller("test", aster::ExecutionKind::kThread, 1);\n'
        "  assert(client.CallAsync(request, 100, completion, Complete, &result, caller) ==\n"
        "         aster::Status::kOk);\n"
        "  assert(result.called && result.sum == 42);\n"
        "  assert(!completion.pending());\n"
        "  return 0;\n"
        "}\n",
    )


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
@pytest.mark.parametrize(
    ("method", "kind"),
    [
        ("rpc Watch(stream Request) returns (Response);", "client-streaming"),
        ("rpc Watch(Request) returns (stream Response);", "server-streaming"),
        (
            "rpc Watch(stream Request) returns (stream Response);",
            "bidirectional-streaming",
        ),
    ],
)
def test_rejects_streaming_rpc(tmp_path: Path, method: str, kind: str) -> None:
    proto = tmp_path / "streaming.proto"
    proto.write_text(
        'syntax = "proto3"; package bad; message Request {} message Response {} '
        f"service Streaming {{ {method} }}\n",
        encoding="utf-8",
    )

    with pytest.raises(
        ProtobufProfileError,
        match=rf"{kind} RPC is not supported: bad\.Streaming\.Watch",
    ):
        generate_from_proto([proto], tmp_path / "streaming.pb.hpp", None, [tmp_path])


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_descriptor_creation_normalizes_relative_proto_paths(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    proto = tmp_path / "schema/sample.proto"
    proto.parent.mkdir()
    proto.write_text(PROTO, encoding="utf-8")
    monkeypatch.chdir(tmp_path)

    descriptor = create_descriptor_set(
        [Path("schema/sample.proto")], Path("generated/schema.pb"), [Path("schema")]
    )

    document = descriptor_pb2.FileDescriptorSet.FromString(descriptor.read_bytes())
    assert [item.name for item in document.file] == ["sample.proto"]


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_decoder_rejects_malformed_input_and_runs_fixed_fuzz_corpus(
    tmp_path: Path,
) -> None:
    proto = tmp_path / "limit.proto"
    proto.write_text(
        'syntax = "proto3"; package wire.v1; message Limit {'
        " repeated uint32 values = 1; string text = 2; bytes raw = 3; }\n",
        encoding="utf-8",
    )
    bounds = tmp_path / "bounds.yaml"
    bounds.write_text(
        "fields:\n"
        "  wire.v1.Limit.values: {max_count: 2}\n"
        "  wire.v1.Limit.text: {max_size: 3}\n"
        "  wire.v1.Limit.raw: {max_size: 3}\n",
        encoding="utf-8",
    )
    generate_from_proto([proto], tmp_path / "bounded.pb.hpp", bounds, [tmp_path])

    _compile_and_run(
        tmp_path,
        "#include <array>\n"
        "#include <cassert>\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <span>\n"
        "#include <string_view>\n"
        '#include "bounded.pb.hpp"\n'
        "template <std::size_t Size>\n"
        "aster::Status Decode(const std::array<std::byte, Size>& input) {\n"
        "  wire::v1::Limit output;\n"
        "  return aster::TypeSupport<wire::v1::Limit>::Decode(input, output);\n"
        "}\n"
        "int main() {\n"
        "  constexpr std::array unknown_fields{\n"
        "      std::byte{0x08}, std::byte{0x07},\n"
        "      std::byte{0x50}, std::byte{0x96}, std::byte{0x01},\n"
        "      std::byte{0x59}, std::byte{0x00}, std::byte{0x00},\n"
        "      std::byte{0x00}, std::byte{0x00}, std::byte{0x00},\n"
        "      std::byte{0x00}, std::byte{0x00}, std::byte{0x00},\n"
        "      std::byte{0x62}, std::byte{0x02}, std::byte{0xaa},\n"
        "      std::byte{0xbb}, std::byte{0x6d}, std::byte{0x00},\n"
        "      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};\n"
        "  wire::v1::Limit decoded;\n"
        "  assert(aster::TypeSupport<wire::v1::Limit>::Decode(\n"
        "             unknown_fields, decoded) == aster::Status::kOk);\n"
        "  assert(decoded.values.size == 1 && decoded.values[0] == 7U);\n"
        "  constexpr std::array capacity_packed{\n"
        "      std::byte{0x0a}, std::byte{0x03}, std::byte{0x01},\n"
        "      std::byte{0x02}, std::byte{0x03}};\n"
        "  assert(Decode(capacity_packed) == aster::Status::kCapacityExceeded);\n"
        "  constexpr std::array capacity_unpacked{\n"
        "      std::byte{0x08}, std::byte{0x01}, std::byte{0x08},\n"
        "      std::byte{0x02}, std::byte{0x08}, std::byte{0x03}};\n"
        "  assert(Decode(capacity_unpacked) == aster::Status::kCapacityExceeded);\n"
        "  constexpr std::array long_string{\n"
        "      std::byte{0x12}, std::byte{0x04}, std::byte{'a'},\n"
        "      std::byte{'b'}, std::byte{'c'}, std::byte{'d'}};\n"
        "  assert(Decode(long_string) == aster::Status::kCapacityExceeded);\n"
        "  constexpr std::array long_bytes{\n"
        "      std::byte{0x1a}, std::byte{0x04}, std::byte{0x00},\n"
        "      std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};\n"
        "  assert(Decode(long_bytes) == aster::Status::kCapacityExceeded);\n"
        "  constexpr std::array truncated_varint{std::byte{0x08},\n"
        "                                           std::byte{0x80}};\n"
        "  assert(Decode(truncated_varint) == aster::Status::kProtocolError);\n"
        "  constexpr std::array truncated_packed{\n"
        "      std::byte{0x0a}, std::byte{0x01}, std::byte{0x80}};\n"
        "  assert(Decode(truncated_packed) == aster::Status::kProtocolError);\n"
        "  constexpr std::array truncated_fixed{std::byte{0x59},\n"
        "                                          std::byte{0x00}};\n"
        "  assert(Decode(truncated_fixed) == aster::Status::kProtocolError);\n"
        "  constexpr std::array truncated_length{\n"
        "      std::byte{0x62}, std::byte{0x02}, std::byte{0x00}};\n"
        "  assert(Decode(truncated_length) == aster::Status::kProtocolError);\n"
        "  constexpr std::array overflowing_length{\n"
        "      std::byte{0x12}, std::byte{0xff}, std::byte{0xff},\n"
        "      std::byte{0xff}, std::byte{0xff}, std::byte{0xff},\n"
        "      std::byte{0xff}, std::byte{0xff}, std::byte{0xff},\n"
        "      std::byte{0xff}, std::byte{0xff}};\n"
        "  assert(Decode(overflowing_length) == aster::Status::kProtocolError);\n"
        "  constexpr std::array invalid_utf8{\n"
        "      std::byte{0x12}, std::byte{0x02}, std::byte{0xc0},\n"
        "      std::byte{0x80}};\n"
        "  assert(Decode(invalid_utf8) == aster::Status::kProtocolError);\n"
        "  constexpr std::array unknown_group{\n"
        "      std::byte{0x53}, std::byte{0x08}, std::byte{0x01},\n"
        "      std::byte{0x5b}, std::byte{0x10}, std::byte{0x02},\n"
        "      std::byte{0x5c}, std::byte{0x54}};\n"
        "  assert(Decode(unknown_group) == aster::Status::kOk);\n"
        "  constexpr std::array unclosed_group{std::byte{0x53}};\n"
        "  constexpr std::array mismatched_group{std::byte{0x53},\n"
        "                                        std::byte{0x5c}};\n"
        "  constexpr std::array bare_end_group{std::byte{0x54}};\n"
        "  constexpr std::array illegal_wire{std::byte{0x56}};\n"
        "  constexpr std::array zero_field{std::byte{0x00}};\n"
        "  constexpr std::array truncated_tag{std::byte{0x80}};\n"
        "  constexpr std::array oversized_tag{\n"
        "      std::byte{0x80}, std::byte{0x80}, std::byte{0x80},\n"
        "      std::byte{0x80}, std::byte{0x10}};\n"
        "  assert(Decode(unclosed_group) == aster::Status::kProtocolError);\n"
        "  assert(Decode(mismatched_group) == aster::Status::kProtocolError);\n"
        "  assert(Decode(bare_end_group) == aster::Status::kProtocolError);\n"
        "  assert(Decode(illegal_wire) == aster::Status::kProtocolError);\n"
        "  assert(Decode(zero_field) == aster::Status::kProtocolError);\n"
        "  assert(Decode(truncated_tag) == aster::Status::kProtocolError);\n"
        "  assert(Decode(oversized_tag) == aster::Status::kProtocolError);\n"
        "  wire::v1::Limit invalid_encode;\n"
        '  assert(invalid_encode.text.assign(std::string_view("\\xc0\\x80", 2)));\n'
        "  std::array<std::byte, wire::v1::Limit::kMaxWireSize> output{};\n"
        "  std::size_t written{};\n"
        "  assert(aster::TypeSupport<wire::v1::Limit>::Encode(\n"
        "             invalid_encode, output, written) ==\n"
        "         aster::Status::kInvalidArgument);\n"
        "  wire::v1::Limit encode_value;\n"
        "  assert(encode_value.values.push_back(1U));\n"
        "  std::array<std::byte, 1> tiny{};\n"
        "  assert(aster::TypeSupport<wire::v1::Limit>::Encode(\n"
        "             encode_value, tiny, written) ==\n"
        "         aster::Status::kCapacityExceeded);\n"
        "  std::uint32_t random = 0x5eed1234U;\n"
        "  std::array<std::byte, 64> corpus{};\n"
        "  std::size_t protocol_errors{};\n"
        "  for (std::size_t iteration = 0; iteration < 4096; ++iteration) {\n"
        "    random ^= random << 13U;\n"
        "    random ^= random >> 17U;\n"
        "    random ^= random << 5U;\n"
        "    const auto size = static_cast<std::size_t>(random % corpus.size());\n"
        "    for (std::size_t index = 0; index < size; ++index) {\n"
        "      random ^= random << 13U;\n"
        "      random ^= random >> 17U;\n"
        "      random ^= random << 5U;\n"
        "      corpus[index] = static_cast<std::byte>(random & 0xffU);\n"
        "    }\n"
        "    wire::v1::Limit fuzzed;\n"
        "    const auto status = aster::TypeSupport<wire::v1::Limit>::Decode(\n"
        "        std::span<const std::byte>(corpus.data(), size), fuzzed);\n"
        "    assert(status != aster::Status::kInvalidArgument);\n"
        "    assert(status == aster::Status::kOk ||\n"
        "           status == aster::Status::kProtocolError ||\n"
        "           status == aster::Status::kCapacityExceeded);\n"
        "    if (status == aster::Status::kProtocolError) ++protocol_errors;\n"
        "    if (status == aster::Status::kOk) {\n"
        "      std::array<std::byte, wire::v1::Limit::kMaxWireSize> encoded{};\n"
        "      assert(aster::TypeSupport<wire::v1::Limit>::Encode(\n"
        "                 fuzzed, encoded, written) == aster::Status::kOk);\n"
        "      assert(written <= encoded.size());\n"
        "    }\n"
        "  }\n"
        "  assert(protocol_errors != 0);\n"
        "  return 0;\n"
        "}\n",
        sanitize=True,
    )


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_wire_compatibility_for_supported_profile(tmp_path: Path) -> None:
    proto = tmp_path / "wire.proto"
    proto.write_text(WIRE_PROTO, encoding="utf-8")
    bounds = tmp_path / "bounds.yaml"
    bounds.write_text(
        "fields:\n"
        "  wire.v1.Child.label: {max_size: 8}\n"
        "  wire.v1.Everything.text: {max_size: 8}\n"
        "  wire.v1.Everything.raw: {max_size: 4}\n"
        "  wire.v1.Everything.packed_values: {max_count: 4}\n"
        "  wire.v1.Everything.unpacked_values: {max_count: 2}\n"
        "  wire.v1.Everything.children: {max_count: 2}\n"
        "  wire.v1.Everything.labels: {max_count: 2, max_size: 8}\n"
        "  wire.v1.Everything.optional_text: {max_size: 4}\n"
        "  wire.v1.Everything.optional_raw: {max_size: 4}\n",
        encoding="utf-8",
    )
    descriptor_path = tmp_path / "wire.pb"
    create_descriptor_set([proto], descriptor_path, [tmp_path])
    output = tmp_path / "bounded.pb.hpp"
    generate_from_proto([proto], output, bounds, [tmp_path])

    message_type, _ = _dynamic_message(descriptor_path, "wire.v1.Everything")
    reference = message_type()
    reference.i32 = -1
    reference.i64 = -(2**63)
    reference.u32 = 4_000_000_000
    reference.u64 = 2**63 + 7
    reference.s32 = -(2**31)
    reference.s64 = 2**62
    reference.fixed32_value = 0xFEDCBA98
    reference.fixed64_value = 0xFEDCBA9876543210
    reference.sfixed32_value = -123_456
    reference.sfixed64_value = -1_234_567_890_123
    reference.float_value = 1.5
    reference.double_value = -2.25
    reference.flag = True
    reference.mode = 2
    reference.text = "μ"
    reference.raw = b"\x00\xff"
    reference.child.delta = -4
    reference.child.label = "kid"
    reference.maybe = 0
    reference.packed_values.extend([-1, 0, 150, -(2**31)])
    reference.unpacked_values.extend([1, 0xFFFFFFFF])
    child = reference.children.add()
    child.delta = 7
    child.label = "child"
    reference.labels.extend(["a", "β"])
    reference.optional_text = ""
    reference.optional_raw = b""
    golden = reference.SerializeToString(deterministic=True)
    golden_values = ", ".join(f"std::byte{{0x{byte:02x}}}" for byte in golden)

    _compile_and_run(
        tmp_path,
        "#include <array>\n"
        "#include <cassert>\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <limits>\n"
        '#include "bounded.pb.hpp"\n'
        "int main() {\n"
        "  wire::v1::Everything value;\n"
        "  value.i32 = -1;\n"
        "  value.i64 = std::numeric_limits<std::int64_t>::min();\n"
        "  value.u32 = 4000000000U;\n"
        "  value.u64 = 9223372036854775815ULL;\n"
        "  value.s32 = std::numeric_limits<std::int32_t>::min();\n"
        "  value.s64 = 4611686018427387904LL;\n"
        "  value.fixed32_value = 0xfedcba98U;\n"
        "  value.fixed64_value = 0xfedcba9876543210ULL;\n"
        "  value.sfixed32_value = -123456;\n"
        "  value.sfixed64_value = -1234567890123LL;\n"
        "  value.float_value = 1.5F;\n"
        "  value.double_value = -2.25;\n"
        "  value.flag = true;\n"
        "  value.mode = wire::v1::Mode::MODE_ACTIVE;\n"
        '  assert(value.text.assign("\\xce\\xbc"));\n'
        "  assert(value.raw.push_back(std::byte{0x00}));\n"
        "  assert(value.raw.push_back(std::byte{0xff}));\n"
        "  value.child.emplace();\n"
        "  value.child->delta = -4;\n"
        '  assert(value.child->label.assign("kid"));\n'
        "  value.maybe = 0U;\n"
        "  assert(value.packed_values.push_back(-1));\n"
        "  assert(value.packed_values.push_back(0));\n"
        "  assert(value.packed_values.push_back(150));\n"
        "  assert(value.packed_values.push_back(\n"
        "      std::numeric_limits<std::int32_t>::min()));\n"
        "  assert(value.unpacked_values.push_back(1U));\n"
        "  assert(value.unpacked_values.push_back(0xffffffffU));\n"
        "  wire::v1::Child child;\n"
        "  child.delta = 7;\n"
        '  assert(child.label.assign("child"));\n'
        "  assert(value.children.push_back(child));\n"
        "  aster::proto::BoundedString<8> label;\n"
        '  assert(label.assign("a"));\n'
        "  assert(value.labels.push_back(label));\n"
        '  assert(label.assign("\\xce\\xb2"));\n'
        "  assert(value.labels.push_back(label));\n"
        "  value.optional_text.emplace();\n"
        '  assert(value.optional_text->assign(""));\n'
        "  value.optional_raw.emplace();\n"
        f"  constexpr std::array golden{{{golden_values}}};\n"
        "  std::array<std::byte, wire::v1::Everything::kMaxWireSize> encoded{};\n"
        "  std::size_t written{};\n"
        "  assert(aster::TypeSupport<wire::v1::Everything>::Encode(\n"
        "             value, encoded, written) == aster::Status::kOk);\n"
        "  assert(written == golden.size());\n"
        "  for (std::size_t index = 0; index < written; ++index)\n"
        "    assert(encoded[index] == golden[index]);\n"
        "  wire::v1::Everything decoded;\n"
        "  assert(aster::TypeSupport<wire::v1::Everything>::Decode(\n"
        "             golden, decoded) == aster::Status::kOk);\n"
        "  assert(decoded.i32 == -1);\n"
        "  assert(decoded.i64 == std::numeric_limits<std::int64_t>::min());\n"
        "  assert(decoded.s32 == std::numeric_limits<std::int32_t>::min());\n"
        "  assert(decoded.text.view() == value.text.view());\n"
        "  assert(decoded.raw.size == 2);\n"
        "  assert(decoded.child.has_value());\n"
        "  assert(decoded.child->delta == -4);\n"
        '  assert(decoded.child->label.view() == "kid");\n'
        "  assert(decoded.maybe.has_value() && *decoded.maybe == 0U);\n"
        "  assert(decoded.packed_values.size == 4);\n"
        "  assert(decoded.unpacked_values.size == 2);\n"
        "  assert(decoded.children.size == 1);\n"
        "  assert(decoded.labels.size == 2);\n"
        "  assert(decoded.optional_text.has_value());\n"
        "  assert(decoded.optional_raw.has_value());\n"
        "  static_assert(aster::MessageType<wire::v1::Everything>);\n"
        "  static_assert(aster::TypeSupport<wire::v1::Everything>::descriptor()\n"
        "                    .max_serialized_size ==\n"
        "                wire::v1::Everything::kMaxWireSize);\n"
        "  return 0;\n"
        "}\n",
    )


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_singular_message_preserves_explicit_empty_presence(tmp_path: Path) -> None:
    proto = tmp_path / "presence.proto"
    proto.write_text(
        'syntax = "proto3"; package presence.v1; message Empty {}'
        " message Holder { Empty child = 1; }\n",
        encoding="utf-8",
    )
    descriptor_path = tmp_path / "presence.pb"
    create_descriptor_set([proto], descriptor_path, [tmp_path])
    generate_from_proto([proto], tmp_path / "bounded.pb.hpp", None, [tmp_path])
    holder_type, _ = _dynamic_message(descriptor_path, "presence.v1.Holder")
    unset = holder_type().SerializeToString(deterministic=True)
    present_message = holder_type()
    present_message.child.SetInParent()
    present = present_message.SerializeToString(deterministic=True)
    assert unset == b""
    assert present == b"\x0a\x00"
    present_values = ", ".join(f"std::byte{{0x{byte:02x}}}" for byte in present)

    _compile_and_run(
        tmp_path,
        "#include <array>\n"
        "#include <cassert>\n"
        "#include <cstddef>\n"
        '#include "bounded.pb.hpp"\n'
        "int main() {\n"
        "  constexpr std::array<std::byte, 0> unset{};\n"
        f"  constexpr std::array present{{{present_values}}};\n"
        "  presence::v1::Holder value;\n"
        "  std::array<std::byte, presence::v1::Holder::kMaxWireSize> encoded{};\n"
        "  std::size_t written{};\n"
        "  assert(aster::TypeSupport<presence::v1::Holder>::Encode(\n"
        "             value, encoded, written) == aster::Status::kOk);\n"
        "  assert(written == unset.size());\n"
        "  value.child.emplace();\n"
        "  assert(aster::TypeSupport<presence::v1::Holder>::Encode(\n"
        "             value, encoded, written) == aster::Status::kOk);\n"
        "  assert(written == present.size());\n"
        "  for (std::size_t index = 0; index < written; ++index)\n"
        "    assert(encoded[index] == present[index]);\n"
        "  presence::v1::Holder decoded;\n"
        "  assert(aster::TypeSupport<presence::v1::Holder>::Decode(\n"
        "             present, decoded) == aster::Status::kOk);\n"
        "  assert(decoded.child.has_value());\n"
        "  assert(aster::TypeSupport<presence::v1::Holder>::Decode(\n"
        "             unset, decoded) == aster::Status::kOk);\n"
        "  assert(!decoded.child.has_value());\n"
        "  return 0;\n"
        "}\n",
    )


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_rejects_missing_and_unknown_bounds(tmp_path: Path) -> None:
    proto = tmp_path / "sample.proto"
    proto.write_text(PROTO, encoding="utf-8")
    output = tmp_path / "bounded.pb.hpp"

    with pytest.raises(ProtobufProfileError, match="unbounded string"):
        generate_from_proto([proto], output, None, [tmp_path])

    bounds = tmp_path / "bounds.yaml"
    bounds.write_text("fields:\n  test.v1.Sample.missing: {max_size: 4}\n", encoding="utf-8")
    with pytest.raises(ProtobufProfileError, match="unknown fields"):
        generate_from_proto([proto], output, bounds, [tmp_path])

    bounds.write_text("fields:\n  test.v1.Sample.name: {max_size: 0}\n", encoding="utf-8")
    with pytest.raises(ProtobufProfileError, match="positive integer"):
        generate_from_proto([proto], output, bounds, [tmp_path])

    bounds.write_text(
        "fields:\n"
        "  test.v1.Sample.name: {max_size: 16}\n"
        "  test.v1.Sample.values: {max_count: 4, max_size: 4}\n",
        encoding="utf-8",
    )
    with pytest.raises(ProtobufProfileError, match="cannot set max_size"):
        generate_from_proto([proto], output, bounds, [tmp_path])


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
@pytest.mark.parametrize(
    ("source", "message"),
    [
        (
            'syntax = "proto2"; package bad; message Bad { required int32 x = 1; }',
            "only proto3",
        ),
        (
            'syntax = "proto3"; package bad; message Bad { map<string, int32> values = 1; }',
            "map fields",
        ),
        (
            'syntax = "proto3"; package bad; message Bad { Bad next = 1; }',
            "recursive or unresolved",
        ),
        (
            'syntax = "proto3"; package bad; message Bad {'
            " message Inner { int32 x = 1; } Inner inner = 1; }",
            "nested declarations",
        ),
        (
            'syntax = "proto3"; package bad; message Bad {'
            " enum Inner { ZERO = 0; } Inner value = 1; }",
            "nested declarations",
        ),
        (
            'syntax = "proto3"; package bad; message Bad {'
            " oneof selection { int32 x = 1; string y = 2; } }",
            "oneof",
        ),
    ],
)
def test_rejects_features_outside_bounded_profile(
    tmp_path: Path, source: str, message: str
) -> None:
    proto = tmp_path / "bad.proto"
    proto.write_text(source, encoding="utf-8")

    with pytest.raises(ProtobufProfileError, match=message):
        generate_from_proto([proto], tmp_path / "bad.pb.hpp", None, [tmp_path])


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_rejects_google_protobuf_any(tmp_path: Path) -> None:
    executable = Path(shutil.which("protoc") or "").resolve()
    candidates = [
        executable.parent.parent / "include",
        Path("/usr/local/include"),
        Path("/usr/include"),
    ]
    protobuf_include = next(
        (path for path in candidates if (path / "google/protobuf/any.proto").is_file()),
        None,
    )
    if protobuf_include is None:
        pytest.skip("protobuf well-known type sources unavailable")
    proto = tmp_path / "bad.proto"
    proto.write_text(
        'syntax = "proto3"; package bad; import "google/protobuf/any.proto";'
        " message Bad { google.protobuf.Any value = 1; }",
        encoding="utf-8",
    )

    with pytest.raises(ProtobufProfileError, match="Any is not supported"):
        generate_from_proto(
            [proto],
            tmp_path / "bad.pb.hpp",
            None,
            [tmp_path, protobuf_include],
        )


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc unavailable")
def test_orders_cross_package_message_dependencies(tmp_path: Path) -> None:
    common = tmp_path / "common.proto"
    common.write_text(
        'syntax = "proto3"; package z; message Common { string label = 1; }\n',
        encoding="utf-8",
    )
    use = tmp_path / "use.proto"
    use.write_text(
        'syntax = "proto3"; package a; import "common.proto"; '
        "message Use { z.Common common = 1; }\n",
        encoding="utf-8",
    )
    bounds = tmp_path / "bounds.yaml"
    bounds.write_text("fields:\n  z.Common.label: {max_size: 8}\n", encoding="utf-8")
    output = tmp_path / "bounded.pb.hpp"

    first = generate_from_proto([common, use], output, bounds, [tmp_path])
    reversed_output = tmp_path / "reversed.pb.hpp"
    second = generate_from_proto([use, common], reversed_output, bounds, [tmp_path])

    assert first.schema_hash == second.schema_hash
    assert output.read_bytes() == reversed_output.read_bytes()

    compiler = shutil.which("c++")
    if compiler:
        source = tmp_path / "check.cpp"
        source.write_text(
            '#include "bounded.pb.hpp"\n'
            "int main() { a::Use value; value.common.emplace(); "
            'return value.common->label.assign("ok") ? 0 : 1; }\n',
            encoding="utf-8",
        )
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(tmp_path),
                "-I",
                str(REPOSITORY / "include"),
                str(source),
                "-o",
                str(tmp_path / "check"),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
