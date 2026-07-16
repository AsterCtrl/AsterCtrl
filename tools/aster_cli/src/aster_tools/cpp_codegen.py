"""Deterministic C++20 code generation for interface models."""

from __future__ import annotations

from collections.abc import Iterable

from aster_tools.interface_model import (
    PRIMITIVE_CPP_TYPES,
    PRIMITIVE_SIZES,
    EnumDefinition,
    FieldDefinition,
    InterfaceError,
    InterfaceModel,
    RecordDefinition,
    resolve_symbol,
)


def cpp_namespace(namespace: str) -> str:
    return namespace.replace(".", "::")


def qualified(full_name: str) -> str:
    return "::" + full_name.replace(".", "::")


def cpp_type(record: RecordDefinition, field: FieldDefinition, model: InterfaceModel) -> str:
    symbol = resolve_symbol(record, field.type_name, model)
    return PRIMITIVE_CPP_TYPES.get(symbol, qualified(symbol))


def topological_records(model: InterfaceModel) -> list[RecordDefinition]:
    ordered: list[RecordDefinition] = []
    complete: set[str] = set()
    visiting: set[str] = set()

    def visit(record: RecordDefinition) -> None:
        if record.full_name in complete:
            return
        if record.full_name in visiting:
            raise InterfaceError(f"recursive value type involving {record.full_name}")
        visiting.add(record.full_name)
        for field in record.fields:
            symbol = resolve_symbol(record, field.type_name, model)
            if symbol in model.records:
                visit(model.records[symbol])
        visiting.remove(record.full_name)
        complete.add(record.full_name)
        ordered.append(record)

    for item in sorted(model.records.values(), key=lambda value: value.full_name):
        visit(item)
    return ordered


def record_wire_sizes(model: InterfaceModel) -> dict[str, int]:
    sizes: dict[str, int] = {}

    def size(record: RecordDefinition, visiting: set[str]) -> int:
        if record.full_name in sizes:
            return sizes[record.full_name]
        if record.full_name in visiting:
            raise InterfaceError(f"recursive value type involving {record.full_name}")
        visiting.add(record.full_name)
        total = 0
        for field in record.fields:
            symbol = resolve_symbol(record, field.type_name, model)
            if symbol in PRIMITIVE_SIZES:
                field_size = PRIMITIVE_SIZES[symbol]
            elif symbol in model.enums:
                field_size = PRIMITIVE_SIZES[model.enums[symbol].underlying_type]
            else:
                field_size = size(model.records[symbol], visiting)
            total += field_size * (field.array or 1)
        visiting.remove(record.full_name)
        sizes[record.full_name] = total
        return total

    for record in model.records.values():
        size(record, set())
    return sizes


def render_hash(value: str) -> str:
    items = ", ".join(
        f"std::byte{{0x{value[index:index + 2]}}}" for index in range(0, 32, 2)
    )
    return f"SchemaHash{{{{{items}}}}}"


def render_enum(enum: EnumDefinition) -> str:
    lines = [
        f"namespace {cpp_namespace(enum.namespace)} {{",
        "",
        f"enum class {enum.name} : {PRIMITIVE_CPP_TYPES[enum.underlying_type]} {{",
    ]
    for value in enum.values:
        lines.append(f"  {value.name} = {value.value},")
    lines.extend(["};", "", f"}}  // namespace {cpp_namespace(enum.namespace)}", ""])
    return "\n".join(lines)


def render_default(
    record: RecordDefinition, field: FieldDefinition, model: InterfaceModel
) -> str:
    if field.default is None:
        return "{}"
    symbol = resolve_symbol(record, field.type_name, model)
    if symbol in model.enums:
        return "{" + qualified(symbol) + f"::{field.default}" + "}"
    if symbol == "bool":
        return "{true}" if field.default else "{false}"
    if symbol == "float32":
        return "{" + f"{float(field.default):.9g}F" + "}"
    if symbol == "float64":
        return "{" + f"{float(field.default):.17g}" + "}"
    return "{" + str(field.default) + "}"


def render_record(record: RecordDefinition, model: InterfaceModel) -> str:
    lines = [f"namespace {cpp_namespace(record.namespace)} {{", "", f"struct {record.name} {{"]
    for field in record.fields:
        value_type = cpp_type(record, field, model)
        if field.array is not None:
            lines.append(
                f"  std::array<{value_type}, {field.array}> {field.name}{{}};"
            )
        else:
            lines.append(
                f"  {value_type} {field.name}{render_default(record, field, model)};"
            )
    lines.extend(["};", "", f"}}  // namespace {cpp_namespace(record.namespace)}", ""])
    return "\n".join(lines)


def render_interface_tag(namespace: str, name: str) -> str:
    ns = cpp_namespace(namespace)
    return f"namespace {ns} {{\n\nstruct {name} {{}};\n\n}}  // namespace {ns}\n"


def _render_one_encode(
    record: RecordDefinition,
    field: FieldDefinition,
    expression: str,
    model: InterfaceModel,
    sizes: dict[str, int],
    indent: str,
) -> list[str]:
    symbol = resolve_symbol(record, field.type_name, model)
    if symbol in PRIMITIVE_SIZES or symbol in model.enums:
        return [
            f"{indent}if (const auto status = ::aster_interfaces::detail::WriteScalar({expression}, output, offset);",
            f"{indent}    !IsOk(status)) {{",
            f"{indent}  return status;",
            f"{indent}}}",
        ]
    nested_size = sizes[symbol]
    return [
        f"{indent}std::size_t nested_written{{}};",
        f"{indent}if (const auto status = TypeSupport<{qualified(symbol)}>::Encode(",
        f"{indent}        {expression}, output.subspan(offset, {nested_size}), nested_written);",
        f"{indent}    !IsOk(status)) {{",
        f"{indent}  return status;",
        f"{indent}}}",
        f"{indent}offset += nested_written;",
    ]


def render_encode_fields(
    record: RecordDefinition, model: InterfaceModel, sizes: dict[str, int]
) -> list[str]:
    lines: list[str] = []
    for field in record.fields:
        expression = f"message.{field.name}"
        if field.array is None:
            lines.extend(
                _render_one_encode(record, field, expression, model, sizes, "    ")
            )
            continue
        lines.append(f"    for (const auto& element : {expression}) {{")
        lines.extend(_render_one_encode(record, field, "element", model, sizes, "      "))
        lines.append("    }")
    return lines


def _render_one_decode(
    record: RecordDefinition,
    field: FieldDefinition,
    expression: str,
    model: InterfaceModel,
    sizes: dict[str, int],
    indent: str,
) -> list[str]:
    symbol = resolve_symbol(record, field.type_name, model)
    if symbol in PRIMITIVE_SIZES or symbol in model.enums:
        return [
            f"{indent}if (const auto status = ::aster_interfaces::detail::ReadScalar(input, offset, {expression});",
            f"{indent}    !IsOk(status)) {{",
            f"{indent}  return status;",
            f"{indent}}}",
        ]
    nested_size = sizes[symbol]
    return [
        f"{indent}if (const auto status = TypeSupport<{qualified(symbol)}>::Decode(",
        f"{indent}        input.subspan(offset, {nested_size}), {expression});",
        f"{indent}    !IsOk(status)) {{",
        f"{indent}  return status;",
        f"{indent}}}",
        f"{indent}offset += {nested_size};",
    ]


def render_decode_fields(
    record: RecordDefinition, model: InterfaceModel, sizes: dict[str, int]
) -> list[str]:
    lines: list[str] = []
    for field in record.fields:
        expression = f"message.{field.name}"
        if field.array is None:
            lines.extend(
                _render_one_decode(record, field, expression, model, sizes, "    ")
            )
            continue
        lines.append(f"    for (auto& element : {expression}) {{")
        lines.extend(_render_one_decode(record, field, "element", model, sizes, "      "))
        lines.append("    }")
    return lines


def render_type_support(
    record: RecordDefinition, model: InterfaceModel, sizes: dict[str, int]
) -> str:
    size = sizes[record.full_name]
    lines = [
        "namespace aster::runtime {",
        "",
        "template <>",
        f"struct TypeSupport<{qualified(record.full_name)}> {{",
        "  static constexpr TypeDescriptor descriptor() noexcept {",
        f"    return {{\"{record.full_name}\", {render_hash(record.schema_hash)}, {size}}};",
        "  }",
        "",
        f"  static Status Encode(const {qualified(record.full_name)}& message,",
        "                       std::span<std::byte> output,",
        "                       std::size_t& written) noexcept {",
        "    written = 0;",
        f"    if (output.size() < {size}) {{",
        "      return Status::kCapacityExceeded;",
        "    }",
        "    std::size_t offset{};",
    ]
    lines.extend(render_encode_fields(record, model, sizes))
    lines.extend(
        [
            "    written = offset;",
            f"    return offset == {size} ? Status::kOk : Status::kInternal;",
            "  }",
            "",
            "  static Status Decode(std::span<const std::byte> input,",
            f"                       {qualified(record.full_name)}& message) noexcept {{",
            f"    if (input.size() != {size}) {{",
            "      return Status::kInvalidArgument;",
            "    }",
            "    std::size_t offset{};",
        ]
    )
    lines.extend(render_decode_fields(record, model, sizes))
    lines.extend(
        [
            f"    return offset == {size} ? Status::kOk : Status::kInternal;",
            "  }",
            "};",
            "",
            "}  // namespace aster::runtime",
            "",
        ]
    )
    return "\n".join(lines)


def render_service_support(interface) -> str:
    full_name = interface.full_name
    request = qualified(f"{full_name}Request")
    response = qualified(f"{full_name}Response")
    return "\n".join(
        [
            "namespace aster::runtime {",
            "",
            "template <>",
            f"struct ServiceTypeSupport<{qualified(full_name)}> {{",
            f"  using Request = {request};",
            f"  using Response = {response};",
            "",
            "  static constexpr ServiceDescriptor descriptor() noexcept {",
            f"    return {{\"{full_name}\", {render_hash(interface.schema_hash)}}};",
            "  }",
            "};",
            "",
            "}  // namespace aster::runtime",
            "",
        ]
    )


def render_action_support(interface) -> str:
    full_name = interface.full_name
    return "\n".join(
        [
            "namespace aster::runtime {",
            "",
            "template <>",
            f"struct ActionTypeSupport<{qualified(full_name)}> {{",
            f"  using Goal = {qualified(f'{full_name}Goal')};",
            f"  using Feedback = {qualified(f'{full_name}Feedback')};",
            f"  using Result = {qualified(f'{full_name}Result')};",
            "",
            "  static constexpr ActionDescriptor descriptor() noexcept {",
            f"    return {{\"{full_name}\", {render_hash(interface.schema_hash)}}};",
            "  }",
            "};",
            "",
            "}  // namespace aster::runtime",
            "",
        ]
    )


DETAIL_HELPERS = r"""namespace aster_interfaces::detail {

using aster::runtime::Status;

template <typename Value, bool = std::is_enum_v<Value>>
struct WireType {
  using type = Value;
};

template <typename Value>
struct WireType<Value, true> {
  using type = std::underlying_type_t<Value>;
};

template <typename Value>
using WireTypeT = typename WireType<Value>::type;

template <typename Value>
Status WriteScalar(Value value, std::span<std::byte> output,
                   std::size_t& offset) noexcept {
  using Raw = WireTypeT<Value>;
  if (offset + sizeof(Raw) > output.size()) {
    return Status::kCapacityExceeded;
  }
  if constexpr (std::is_same_v<Raw, bool>) {
    output[offset++] = value ? std::byte{1} : std::byte{0};
  } else if constexpr (std::is_floating_point_v<Raw>) {
    using Bits = std::conditional_t<sizeof(Raw) == 4, std::uint32_t,
                                    std::uint64_t>;
    const auto bits = std::bit_cast<Bits>(static_cast<Raw>(value));
    for (std::size_t index = 0; index < sizeof(Raw); ++index) {
      output[offset++] = static_cast<std::byte>(bits >> (index * 8U));
    }
  } else {
    using Unsigned = std::make_unsigned_t<Raw>;
    const auto raw = static_cast<Raw>(value);
    const auto bits = std::bit_cast<Unsigned>(raw);
    for (std::size_t index = 0; index < sizeof(Raw); ++index) {
      output[offset++] = static_cast<std::byte>(bits >> (index * 8U));
    }
  }
  return Status::kOk;
}

template <typename Value>
Status ReadScalar(std::span<const std::byte> input, std::size_t& offset,
                  Value& value) noexcept {
  using Raw = WireTypeT<Value>;
  if (offset + sizeof(Raw) > input.size()) {
    return Status::kInvalidArgument;
  }
  if constexpr (std::is_same_v<Raw, bool>) {
    value = input[offset++] != std::byte{0};
  } else if constexpr (std::is_floating_point_v<Raw>) {
    using Bits = std::conditional_t<sizeof(Raw) == 4, std::uint32_t,
                                    std::uint64_t>;
    Bits bits{};
    for (std::size_t index = 0; index < sizeof(Raw); ++index) {
      bits |= static_cast<Bits>(std::to_integer<std::uint8_t>(input[offset++]))
              << (index * 8U);
    }
    value = static_cast<Value>(std::bit_cast<Raw>(bits));
  } else {
    using Unsigned = std::make_unsigned_t<Raw>;
    std::uint64_t bits{};
    for (std::size_t index = 0; index < sizeof(Raw); ++index) {
      bits |= static_cast<std::uint64_t>(
                  std::to_integer<std::uint8_t>(input[offset++]))
              << (index * 8U);
    }
    const auto narrowed = static_cast<Unsigned>(bits);
    const auto raw = std::bit_cast<Raw>(narrowed);
    value = static_cast<Value>(raw);
  }
  return Status::kOk;
}

}  // namespace aster_interfaces::detail
"""


def join_sections(sections: Iterable[str]) -> str:
    return "\n".join(section.rstrip() for section in sections if section).rstrip() + "\n"


def render_header(model: InterfaceModel) -> str:
    sizes = record_wire_sizes(model)
    records = topological_records(model)
    sections: list[str] = [
        """#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "aster/runtime/action.hpp"
#include "aster/runtime/service.hpp"
#include "aster/runtime/type_support.hpp"

// Generated by aster. Do not edit.
""",
    ]
    sections.extend(render_enum(enum) for enum in sorted(model.enums.values(), key=lambda value: value.full_name))
    sections.extend(render_record(record, model) for record in records)
    for interface in model.interfaces:
        if interface.kind in ("Service", "Action"):
            sections.append(render_interface_tag(interface.namespace, interface.name))
    sections.append(DETAIL_HELPERS)
    sections.extend(render_type_support(record, model, sizes) for record in records)
    for interface in model.interfaces:
        if interface.kind == "Service":
            sections.append(render_service_support(interface))
        elif interface.kind == "Action":
            sections.append(render_action_support(interface))
    return join_sections(sections)
