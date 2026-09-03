from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

from aster_tools.interfaces import InterfaceError, generate_interfaces


MESSAGE = """\
api_version: aster.dev/schema/v1alpha1
kind: Message
metadata:
  name: Command
  namespace: test.msg
spec:
  enums:
    - name: Mode
      underlying_type: uint8
      values:
        - {name: kStop, value: 0}
        - {name: kRun, value: 1}
  fields:
    - {name: mode, type: Mode, default: kStop}
    - {name: target, type: float32, unit: rad}
    - {name: samples, type: int16, array: 2}
"""

SERVICE = """\
api_version: aster.dev/schema/v1alpha1
kind: Service
metadata:
  name: SetEnabled
  namespace: test.srv
spec:
  request:
    fields:
      - {name: enabled, type: bool}
  response:
    fields:
      - {name: accepted, type: bool}
"""

ACTION = """\
api_version: aster.dev/schema/v1alpha1
kind: Action
metadata:
  name: Move
  namespace: test.action
spec:
  goal:
    fields:
      - {name: distance, type: float32, unit: m}
  feedback:
    fields:
      - {name: progress, type: uint8, unit: percent}
  result:
    fields:
      - {name: travelled, type: float32, unit: m}
"""


def write_schema(root: Path, relative: str, content: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def test_generates_deterministic_cpp_typesupport_and_lock(tmp_path: Path) -> None:
    schema_root = tmp_path / "schemas"
    output = tmp_path / "generated"
    write_schema(schema_root, "msg/Command.msg.yaml", MESSAGE)
    write_schema(schema_root, "srv/SetEnabled.srv.yaml", SERVICE)
    write_schema(schema_root, "action/Move.action.yaml", ACTION)

    first = generate_interfaces(schema_root, output)
    first_header = (output / "include/aster/interfaces.hpp").read_bytes()
    first_lock = (output / "schema.lock.yaml").read_bytes()
    first_vectors = (output / "test_vectors.yaml").read_bytes()
    second = generate_interfaces(schema_root, output)

    assert first == second
    assert (output / "include/aster/interfaces.hpp").read_bytes() == first_header
    assert (output / "schema.lock.yaml").read_bytes() == first_lock
    assert (output / "test_vectors.yaml").read_bytes() == first_vectors
    text = first_header.decode()
    assert "enum class Mode : std::uint8_t" in text
    assert "struct Command" in text
    assert "TypeSupport<::test::msg::Command>" in text
    assert "ServiceTypeSupport<::test::srv::SetEnabled>" in text
    assert "ActionTypeSupport<::test::action::Move>" in text
    assert "std::uint64_t bits{};" in text
    assert "const auto narrowed = static_cast<Unsigned>(bits);" in text
    assert first.interface_count == 3
    assert first.record_count == 6
    assert "encoded_hex: '000000000000000000'" in first_vectors.decode()


def test_rejects_unbounded_or_unknown_field_types(tmp_path: Path) -> None:
    schema_root = tmp_path / "schemas"
    write_schema(
        schema_root,
        "msg/Bad.msg.yaml",
        MESSAGE.replace("type: float32", "type: string"),
    )

    with pytest.raises(InterfaceError, match="unknown or unbounded type 'string'"):
        generate_interfaces(schema_root, tmp_path / "generated")


def test_schema_hash_changes_when_contract_changes(tmp_path: Path) -> None:
    schema_root = tmp_path / "schemas"
    output = tmp_path / "generated"
    write_schema(schema_root, "msg/Command.msg.yaml", MESSAGE)
    first = generate_interfaces(schema_root, output)

    write_schema(
        schema_root,
        "msg/Command.msg.yaml",
        MESSAGE.replace("type: float32", "type: float64"),
    )
    second = generate_interfaces(schema_root, output)

    assert first.deployment_schema_hash != second.deployment_schema_hash


def test_generated_header_compiles_and_round_trips(tmp_path: Path) -> None:
    schema_root = tmp_path / "schemas"
    output = tmp_path / "generated"
    write_schema(schema_root, "msg/Command.msg.yaml", MESSAGE)
    write_schema(schema_root, "srv/SetEnabled.srv.yaml", SERVICE)
    write_schema(schema_root, "action/Move.action.yaml", ACTION)
    generate_interfaces(schema_root, output)
    source = tmp_path / "generated_test.cpp"
    source.write_text(
        """\
#include <array>
#include <cassert>

#include "aster/interfaces.hpp"

int main() {
  static_assert(aster::runtime::MessageType<test::msg::Command>);
  static_assert(aster::runtime::ServiceType<test::srv::SetEnabled>);
  static_assert(aster::runtime::ActionType<test::action::Move>);
  test::msg::Command input;
  input.mode = test::msg::Mode::kRun;
  input.target = -1.25F;
  input.samples = {123, -456};
  std::array<std::byte, 9> bytes{};
  std::size_t written{};
  assert(aster::runtime::TypeSupport<test::msg::Command>::Encode(
             input, bytes, written) == aster::runtime::Status::kOk);
  assert(written == bytes.size());
  test::msg::Command output;
  assert(aster::runtime::TypeSupport<test::msg::Command>::Decode(
             bytes, output) == aster::runtime::Status::kOk);
  assert(output.mode == test::msg::Mode::kRun);
  assert(output.target == -1.25F);
  assert(output.samples[0] == 123);
  assert(output.samples[1] == -456);
}
""",
        encoding="utf-8",
    )
    runtime_include = Path(__file__).parents[2] / "aster-runtime" / "include"
    executable = tmp_path / "generated_test"
    subprocess.run(
        [
            "/usr/bin/c++",
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Wconversion",
            "-Werror",
            "-I",
            str(output / "include"),
            "-I",
            str(runtime_include),
            str(source),
            "-o",
            str(executable),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run([str(executable)], check=True)
