"""Bounded protobuf profile and fixed-capacity C++ generator."""

from __future__ import annotations

import hashlib
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml
from google.protobuf import descriptor_pb2
from google.protobuf.message import DecodeError

from .validation import canonical_json


class ProtobufProfileError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class ProtobufResult:
    schema_hash: str
    message_count: int
    output: Path
    messages: tuple[BoundedMessage, ...] = ()
    rpc_methods: tuple[BoundedRpcMethod, ...] = ()
    declarations: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class BoundedMessage:
    """Resolved wire contract for one protobuf message."""

    type_name: str
    max_wire_size: int


@dataclass(frozen=True, slots=True)
class BoundedRpcMethod:
    """Resolved request and response bounds for one unary RPC method."""

    type_name: str
    request_type: str
    response_type: str
    request_max_wire_size: int
    response_max_wire_size: int

    @property
    def max_wire_size(self) -> int:
        return max(self.request_max_wire_size, self.response_max_wire_size)


@dataclass(frozen=True, slots=True)
class BoundedSchema:
    """Canonical descriptor-and-bounds contract shared by codegen and locks."""

    schema_hash: str
    messages: tuple[BoundedMessage, ...]
    rpc_methods: tuple[BoundedRpcMethod, ...] = ()

    def message(self, type_name: str) -> BoundedMessage | None:
        return next((item for item in self.messages if item.type_name == type_name), None)

    def rpc_method(self, type_name: str) -> BoundedRpcMethod | None:
        return next((item for item in self.rpc_methods if item.type_name == type_name), None)

    def max_wire_size(self, type_name: str, *, rpc: bool = False) -> int | None:
        contract = self.rpc_method(type_name) if rpc else self.message(type_name)
        return contract.max_wire_size if contract is not None else None


def _load_bounds(path: str | Path | None) -> dict[str, dict[str, int]]:
    if path is None:
        return {}
    source = Path(path)
    try:
        value = yaml.safe_load(source.read_text(encoding="utf-8")) or {}
    except (OSError, UnicodeError, yaml.YAMLError) as error:
        raise ProtobufProfileError(f"cannot read bounds {source}: {error}") from error
    if not isinstance(value, dict):
        raise ProtobufProfileError("bounds root must be a mapping")
    fields = value.get("fields", value)
    if not isinstance(fields, dict):
        raise ProtobufProfileError("bounds.fields must be a mapping")
    result: dict[str, dict[str, int]] = {}
    for name, options in fields.items():
        if not isinstance(name, str) or not isinstance(options, dict):
            raise ProtobufProfileError("each bound must map a field name to options")
        unknown = sorted(set(options) - {"max_count", "max_size"})
        if unknown:
            raise ProtobufProfileError(f"bound {name!r} has unknown options: {', '.join(unknown)}")
        normalized: dict[str, int] = {}
        for option, amount in options.items():
            if type(amount) is not int or amount <= 0:
                raise ProtobufProfileError(f"bound {name!r}.{option} must be a positive integer")
            normalized[option] = amount
        result[name] = normalized
    return result


def create_descriptor_set(
    proto_files: list[str | Path],
    output: str | Path,
    includes: list[str | Path] | None = None,
    protoc: str = "protoc",
) -> Path:
    destination = Path(output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    command = [protoc, f"--descriptor_set_out={destination}", "--include_imports"]
    for include in includes or []:
        command.append(f"--proto_path={Path(include).resolve()}")
    command.extend(str(Path(item).resolve()) for item in proto_files)
    try:
        subprocess.run(command, check=True, capture_output=True, text=True)
    except FileNotFoundError as error:
        raise ProtobufProfileError(f"protoc executable not found: {protoc}") from error
    except subprocess.CalledProcessError as error:
        raise ProtobufProfileError(error.stderr.strip() or "protoc failed") from error
    return destination


def _qualified(package: str, message: str, field: str | None = None) -> str:
    result = ".".join(part for part in (package, message) if part)
    return f"{result}.{field}" if field else result


def _cpp_name(type_name: str) -> str:
    return "::" + "::".join(part for part in type_name.lstrip(".").split(".") if part)


_SCALARS = {
    descriptor_pb2.FieldDescriptorProto.TYPE_DOUBLE: "double",
    descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT: "float",
    descriptor_pb2.FieldDescriptorProto.TYPE_INT64: "std::int64_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_UINT64: "std::uint64_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_INT32: "std::int32_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_FIXED64: "std::uint64_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_FIXED32: "std::uint32_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_BOOL: "bool",
    descriptor_pb2.FieldDescriptorProto.TYPE_UINT32: "std::uint32_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED32: "std::int32_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED64: "std::int64_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_SINT32: "std::int32_t",
    descriptor_pb2.FieldDescriptorProto.TYPE_SINT64: "std::int64_t",
}

_VARINT_TYPES = {
    descriptor_pb2.FieldDescriptorProto.TYPE_INT32,
    descriptor_pb2.FieldDescriptorProto.TYPE_INT64,
    descriptor_pb2.FieldDescriptorProto.TYPE_UINT32,
    descriptor_pb2.FieldDescriptorProto.TYPE_UINT64,
    descriptor_pb2.FieldDescriptorProto.TYPE_SINT32,
    descriptor_pb2.FieldDescriptorProto.TYPE_SINT64,
    descriptor_pb2.FieldDescriptorProto.TYPE_BOOL,
    descriptor_pb2.FieldDescriptorProto.TYPE_ENUM,
}
_FIXED32_TYPES = {
    descriptor_pb2.FieldDescriptorProto.TYPE_FIXED32,
    descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED32,
    descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT,
}
_FIXED64_TYPES = {
    descriptor_pb2.FieldDescriptorProto.TYPE_FIXED64,
    descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED64,
    descriptor_pb2.FieldDescriptorProto.TYPE_DOUBLE,
}
_PACKABLE_TYPES = _VARINT_TYPES | _FIXED32_TYPES | _FIXED64_TYPES


@dataclass(slots=True)
class _Message:
    package: str
    syntax: str
    descriptor: Any
    full_name: str
    max_wire_size: int = 0


@dataclass(slots=True)
class _Service:
    package: str
    descriptor: Any
    full_name: str


def _has_presence(field: Any) -> bool:
    return field.proto3_optional or (
        field.type == descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE
        and field.label != descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED
    )


def _field_type(field: Any, bound: dict[str, int], name: str) -> str:
    if field.type in _SCALARS:
        item = _SCALARS[field.type]
    elif (
        field.type == descriptor_pb2.FieldDescriptorProto.TYPE_ENUM
        or field.type == descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE
    ):
        item = _cpp_name(field.type_name)
    elif field.type == descriptor_pb2.FieldDescriptorProto.TYPE_STRING:
        if "max_size" not in bound:
            raise ProtobufProfileError(f"unbounded string field {name!r}; set max_size")
        item = f"aster::proto::BoundedString<{bound['max_size']}>"
    elif field.type == descriptor_pb2.FieldDescriptorProto.TYPE_BYTES:
        if "max_size" not in bound:
            raise ProtobufProfileError(f"unbounded bytes field {name!r}; set max_size")
        item = f"aster::proto::BoundedBytes<{bound['max_size']}>"
    else:
        raise ProtobufProfileError(f"field {name!r} uses unsupported protobuf type")
    if "max_size" in bound and field.type not in {
        descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
        descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
    }:
        raise ProtobufProfileError(f"non-string/bytes field {name!r} cannot set max_size")
    if field.label == descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED:
        if "max_count" not in bound:
            raise ProtobufProfileError(f"unbounded repeated field {name!r}; set max_count")
        item = f"aster::proto::BoundedVector<{item}, {bound['max_count']}>"
    elif "max_count" in bound:
        raise ProtobufProfileError(f"non-repeated field {name!r} cannot set max_count")
    if _has_presence(field):
        item = f"std::optional<{item}>"
    return item


def _support_header(schema_hash: str, *, has_services: bool) -> str:
    truncated = ", ".join(
        f"std::byte{{0x{schema_hash[index : index + 2]}}}" for index in range(0, 32, 2)
    )
    rpc_header = '#include "aster/rpc.hpp"\n' if has_services else ""
    return f"""// Generated by aster. Do not edit.
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

{rpc_header}#include "aster/type_support.hpp"

#ifndef ASTER_PROTO_BOUNDED_COMMON_V1
#define ASTER_PROTO_BOUNDED_COMMON_V1
namespace aster::proto {{
template <class T, std::size_t Capacity>
struct BoundedVector {{
  std::array<T, Capacity> values{{}};
  std::size_t size{{}};
  constexpr bool push_back(const T& value) noexcept {{
    if (size == Capacity) return false;
    values[size++] = value;
    return true;
  }}
  constexpr void clear() noexcept {{ size = 0; }}
  constexpr T& operator[](std::size_t index) noexcept {{ return values[index]; }}
  constexpr const T& operator[](std::size_t index) const noexcept {{
    return values[index];
  }}
  constexpr T* begin() noexcept {{ return values.data(); }}
  constexpr T* end() noexcept {{ return values.data() + size; }}
  constexpr const T* begin() const noexcept {{ return values.data(); }}
  constexpr const T* end() const noexcept {{ return values.data() + size; }}
}};

template <std::size_t Capacity>
struct BoundedString {{
  std::array<char, Capacity + 1> values{{}};
  std::size_t size{{}};
  constexpr bool assign(std::string_view value) noexcept {{
    if (value.size() > Capacity) return false;
    for (std::size_t i = 0; i < value.size(); ++i) values[i] = value[i];
    size = value.size();
    values[size] = '\\0';
    return true;
  }}
  constexpr std::string_view view() const noexcept {{ return {{values.data(), size}}; }}
}};

template <std::size_t Capacity>
using BoundedBytes = BoundedVector<std::byte, Capacity>;

namespace detail {{

class Writer {{
 public:
  constexpr explicit Writer(std::span<std::byte> output) noexcept
      : output_(output) {{}}

  constexpr bool Varint(std::uint64_t value) noexcept {{
    do {{
      if (position_ == output_.size()) return false;
      auto byte = static_cast<std::uint8_t>(value & 0x7fU);
      value >>= 7U;
      if (value != 0) byte = static_cast<std::uint8_t>(byte | 0x80U);
      output_[position_++] = static_cast<std::byte>(byte);
    }} while (value != 0);
    return true;
  }}

  constexpr bool Fixed32(std::uint32_t value) noexcept {{
    for (std::size_t index = 0; index < 4; ++index) {{
      if (position_ == output_.size()) return false;
      output_[position_++] = static_cast<std::byte>(value >> (index * 8U));
    }}
    return true;
  }}

  constexpr bool Fixed64(std::uint64_t value) noexcept {{
    for (std::size_t index = 0; index < 8; ++index) {{
      if (position_ == output_.size()) return false;
      output_[position_++] = static_cast<std::byte>(value >> (index * 8U));
    }}
    return true;
  }}

  constexpr bool Tag(std::uint32_t field, std::uint8_t wire_type) noexcept {{
    return Varint((static_cast<std::uint64_t>(field) << 3U) | wire_type);
  }}

  constexpr bool Raw(std::span<const std::byte> value) noexcept {{
    if (value.size() > output_.size() - position_) return false;
    for (const auto byte : value) output_[position_++] = byte;
    return true;
  }}

  constexpr bool LengthDelimited(std::uint32_t field,
                                 std::span<const std::byte> value) noexcept {{
    return Tag(field, 2) && Varint(value.size()) && Raw(value);
  }}

  [[nodiscard]] constexpr std::size_t size() const noexcept {{ return position_; }}

 private:
  std::span<std::byte> output_;
  std::size_t position_{{}};
}};

class Reader {{
 public:
  constexpr explicit Reader(std::span<const std::byte> input) noexcept
      : input_(input) {{}}

  [[nodiscard]] constexpr bool empty() const noexcept {{
    return position_ == input_.size();
  }}

  constexpr bool Varint(std::uint64_t& value) noexcept {{
    value = 0;
    for (std::size_t index = 0; index < 10; ++index) {{
      if (position_ == input_.size()) return false;
      const auto byte = std::to_integer<std::uint8_t>(input_[position_++]);
      if (index == 9 && (byte & 0xfeU) != 0) return false;
      value |= static_cast<std::uint64_t>(byte & 0x7fU) << (index * 7U);
      if ((byte & 0x80U) == 0) return true;
    }}
    return false;
  }}

  constexpr bool Fixed32(std::uint32_t& value) noexcept {{
    if (input_.size() - position_ < 4) return false;
    value = 0;
    for (std::size_t index = 0; index < 4; ++index) {{
      value |= static_cast<std::uint32_t>(
                   std::to_integer<std::uint8_t>(input_[position_++]))
               << (index * 8U);
    }}
    return true;
  }}

  constexpr bool Fixed64(std::uint64_t& value) noexcept {{
    if (input_.size() - position_ < 8) return false;
    value = 0;
    for (std::size_t index = 0; index < 8; ++index) {{
      value |= static_cast<std::uint64_t>(
                   std::to_integer<std::uint8_t>(input_[position_++]))
               << (index * 8U);
    }}
    return true;
  }}

  constexpr bool Bytes(std::span<const std::byte>& value) noexcept {{
    std::uint64_t length{{}};
    if (!Varint(length) || length > input_.size() - position_) return false;
    value = input_.subspan(position_, static_cast<std::size_t>(length));
    position_ += static_cast<std::size_t>(length);
    return true;
  }}

  constexpr bool Field(std::uint32_t& number, std::uint8_t& wire_type) noexcept {{
    std::uint64_t tag{{}};
    if (!Varint(tag)) return false;
    const auto wide_number = tag >> 3U;
    if (wide_number == 0 || wide_number > 0x1fffffffU) return false;
    number = static_cast<std::uint32_t>(wide_number);
    wire_type = static_cast<std::uint8_t>(tag & 0x07U);
    return wire_type <= 5;
  }}

  constexpr bool Skip(std::uint32_t field_number,
                      std::uint8_t wire_type) noexcept {{
    if (wire_type != 3) return SkipValue(wire_type);
    std::array<std::uint32_t, 16> groups{{}};
    std::size_t depth{{1}};
    groups[0] = field_number;
    while (depth != 0) {{
      std::uint32_t nested_field{{}};
      std::uint8_t nested_wire_type{{}};
      if (!Field(nested_field, nested_wire_type)) return false;
      if (nested_wire_type == 3) {{
        if (depth == groups.size()) return false;
        groups[depth++] = nested_field;
      }} else if (nested_wire_type == 4) {{
        if (nested_field != groups[depth - 1]) return false;
        --depth;
      }} else if (!SkipValue(nested_wire_type)) {{
        return false;
      }}
    }}
    return true;
  }}

 private:
  constexpr bool SkipValue(std::uint8_t wire_type) noexcept {{
    std::uint64_t wide{{}};
    std::uint32_t narrow{{}};
    std::span<const std::byte> bytes;
    switch (wire_type) {{
      case 0: return Varint(wide);
      case 1: return Fixed64(wide);
      case 2: return Bytes(bytes);
      case 5: return Fixed32(narrow);
      default: return false;
    }}
  }}

  std::span<const std::byte> input_;
  std::size_t position_{{}};
}};

constexpr std::uint32_t ZigZag32(std::int32_t value) noexcept {{
  const auto bits = std::bit_cast<std::uint32_t>(value);
  return (bits << 1U) ^ (0U - (bits >> 31U));
}}

constexpr std::uint64_t ZigZag64(std::int64_t value) noexcept {{
  const auto bits = std::bit_cast<std::uint64_t>(value);
  return (bits << 1U) ^ (0U - (bits >> 63U));
}}

constexpr std::int32_t UnZigZag32(std::uint32_t value) noexcept {{
  return std::bit_cast<std::int32_t>((value >> 1U) ^ (0U - (value & 1U)));
}}

constexpr std::int64_t UnZigZag64(std::uint64_t value) noexcept {{
  return std::bit_cast<std::int64_t>((value >> 1U) ^ (0U - (value & 1U)));
}}

constexpr bool ValidUtf8(std::span<const std::byte> input) noexcept {{
  std::size_t index{{}};
  while (index < input.size()) {{
    const auto first = std::to_integer<std::uint8_t>(input[index++]);
    if (first <= 0x7fU) continue;
    std::size_t continuation{{}};
    std::uint8_t second_min{{0x80U}};
    std::uint8_t second_max{{0xbfU}};
    if (first >= 0xc2U && first <= 0xdfU) {{
      continuation = 1;
    }} else if (first >= 0xe0U && first <= 0xefU) {{
      continuation = 2;
      if (first == 0xe0U) second_min = 0xa0U;
      if (first == 0xedU) second_max = 0x9fU;
    }} else if (first >= 0xf0U && first <= 0xf4U) {{
      continuation = 3;
      if (first == 0xf0U) second_min = 0x90U;
      if (first == 0xf4U) second_max = 0x8fU;
    }} else {{
      return false;
    }}
    if (input.size() - index < continuation) return false;
    const auto second = std::to_integer<std::uint8_t>(input[index++]);
    if (second < second_min || second > second_max) return false;
    for (std::size_t remaining = 1; remaining < continuation; ++remaining) {{
      const auto byte = std::to_integer<std::uint8_t>(input[index++]);
      if (byte < 0x80U || byte > 0xbfU) return false;
    }}
  }}
  return true;
}}

}}  // namespace detail
}}  // namespace aster::proto
#endif

namespace aster::proto::schema_{schema_hash} {{
inline constexpr std::string_view kSchemaSha256 = "{schema_hash}";
// Runtime SchemaHash uses the first 16 bytes of SHA-256; full digest is above.
inline constexpr aster::SchemaHash kSchemaHash{{{{{truncated}}}}};
}}  // namespace aster::proto::schema_{schema_hash}

"""


def _declaration(package: str, declaration: str) -> str:
    if not package:
        return declaration + "\n"
    namespace = "::".join(package.split("."))
    return f"namespace {namespace} {{\n{declaration}\n}}  // namespace {namespace}\n"


def _varint_size(value: int) -> int:
    size = 1
    while value >= 0x80:
        value >>= 7
        size += 1
    return size


def _scalar_max_size(field_type: int) -> int:
    if field_type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_INT32,
        descriptor_pb2.FieldDescriptorProto.TYPE_INT64,
        descriptor_pb2.FieldDescriptorProto.TYPE_UINT64,
        descriptor_pb2.FieldDescriptorProto.TYPE_ENUM,
    }:
        return 10
    if field_type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_UINT32,
        descriptor_pb2.FieldDescriptorProto.TYPE_SINT32,
    }:
        return 5
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SINT64:
        return 10
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_BOOL:
        return 1
    if field_type in _FIXED32_TYPES:
        return 4
    if field_type in _FIXED64_TYPES:
        return 8
    raise ProtobufProfileError("unsupported scalar protobuf type")


def _wire_type(field: Any) -> int:
    if field.type in _VARINT_TYPES:
        return 0
    if field.type in _FIXED64_TYPES:
        return 1
    if field.type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
        descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
        descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE,
    }:
        return 2
    if field.type in _FIXED32_TYPES:
        return 5
    raise ProtobufProfileError("unsupported protobuf wire type")


def _is_packed(field: Any, syntax: str) -> bool:
    if field.label != descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED:
        return False
    if field.type not in _PACKABLE_TYPES:
        return False
    if field.options.HasField("packed"):
        return field.options.packed
    return syntax == "proto3"


def _codec_name(full_name: str, operation: str) -> str:
    return f"{operation}_{full_name.replace('.', '_')}"


def _message_max_wire_size(
    info: _Message,
    bounds: dict[str, dict[str, int]],
    messages: dict[str, _Message],
) -> int:
    total = 0
    for field in info.descriptor.field:
        name = f"{info.full_name}.{field.name}"
        bound = bounds.get(name, {})
        wire_type = _wire_type(field)
        tag_size = _varint_size((field.number << 3) | wire_type)
        if field.type in _PACKABLE_TYPES:
            item_size = _scalar_max_size(field.type)
            if field.label == descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED:
                count = bound["max_count"]
                if _is_packed(field, info.syntax):
                    payload = count * item_size
                    total += tag_size + _varint_size(payload) + payload
                else:
                    total += count * (tag_size + item_size)
            else:
                total += tag_size + item_size
            continue
        if field.type in {
            descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
            descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
        }:
            payload = bound["max_size"]
        else:
            dependency = messages[field.type_name.lstrip(".")]
            payload = dependency.max_wire_size
        item_size = tag_size + _varint_size(payload) + payload
        count = bound.get("max_count", 1)
        total += count * item_size
    return max(total, 1)


def _scalar_encode_expression(field: Any, expression: str) -> str:
    field_type = field.type
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_BOOL:
        return f"({expression} ? 1U : 0U)"
    if field_type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_INT32,
        descriptor_pb2.FieldDescriptorProto.TYPE_INT64,
    }:
        return f"static_cast<std::uint64_t>(static_cast<std::int64_t>({expression}))"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_ENUM:
        return (
            "static_cast<std::uint64_t>(static_cast<std::int64_t>("
            f"static_cast<std::int32_t>({expression})))"
        )
    if field_type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_UINT32,
        descriptor_pb2.FieldDescriptorProto.TYPE_UINT64,
    }:
        return f"static_cast<std::uint64_t>({expression})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SINT32:
        return f"aster::proto::detail::ZigZag32({expression})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SINT64:
        return f"aster::proto::detail::ZigZag64({expression})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_FIXED32:
        return expression
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_FIXED64:
        return expression
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED32:
        return f"std::bit_cast<std::uint32_t>({expression})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED64:
        return f"std::bit_cast<std::uint64_t>({expression})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT:
        return f"std::bit_cast<std::uint32_t>({expression})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_DOUBLE:
        return f"std::bit_cast<std::uint64_t>({expression})"
    raise ProtobufProfileError("unsupported scalar protobuf type")


def _scalar_writer(field: Any, writer: str, expression: str) -> str:
    encoded = _scalar_encode_expression(field, expression)
    if field.type in _VARINT_TYPES:
        return f"{writer}.Varint({encoded})"
    if field.type in _FIXED32_TYPES:
        return f"{writer}.Fixed32({encoded})"
    return f"{writer}.Fixed64({encoded})"


def _non_default(field: Any, expression: str) -> str:
    if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_BOOL:
        return expression
    if field.type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT,
        descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED32,
    }:
        return f"std::bit_cast<std::uint32_t>({expression}) != 0"
    if field.type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_DOUBLE,
        descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED64,
    }:
        return f"std::bit_cast<std::uint64_t>({expression}) != 0"
    return f"{expression} != static_cast<decltype({expression})>(0)"


def _bytes_expression(field: Any, expression: str) -> str:
    if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_STRING:
        return (
            f"std::as_bytes(std::span<const char>({expression}.values.data(), {expression}.size))"
        )
    return f"std::span<const std::byte>({expression}.values.data(), {expression}.size)"


def _encode_field(
    info: _Message,
    field: Any,
    bounds: dict[str, dict[str, int]],
    messages: dict[str, _Message],
) -> list[str]:
    name = f"{info.full_name}.{field.name}"
    bound = bounds.get(name, {})
    member = f"value.{field.name}"
    number = field.number
    wire_type = _wire_type(field)
    lines: list[str] = []
    if field.label == descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED:
        lines.extend(
            [
                f"  if ({member}.size > {bound['max_count']})",
                "    return aster::Status::kCapacityExceeded;",
            ]
        )
        lines.append(f"  if ({member}.size != 0) {{")
        if field.type in _PACKABLE_TYPES and _is_packed(field, info.syntax):
            capacity = bound["max_count"] * _scalar_max_size(field.type)
            lines.extend(
                [
                    f"    std::array<std::byte, {capacity}> packed{{}};",
                    "    aster::proto::detail::Writer packed_writer(packed);",
                    f"    for (const auto& item : {member}) {{",
                    f"      if (!{_scalar_writer(field, 'packed_writer', 'item')})",
                    "        return aster::Status::kInternal;",
                    "    }",
                    f"    if (!writer.LengthDelimited({number},",
                    "                                {packed.data(), packed_writer.size()}))",
                    "      return aster::Status::kCapacityExceeded;",
                ]
            )
        elif field.type in _PACKABLE_TYPES:
            lines.extend(
                [
                    f"    for (const auto& item : {member}) {{",
                    f"      if (!writer.Tag({number}, {wire_type}) ||",
                    f"          !{_scalar_writer(field, 'writer', 'item')})",
                    "        return aster::Status::kCapacityExceeded;",
                    "    }",
                ]
            )
        elif field.type in {
            descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
            descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
        }:
            lines.append(f"    for (const auto& item : {member}) {{")
            lines.extend(
                [
                    f"      if (item.size > {bound['max_size']})",
                    "        return aster::Status::kCapacityExceeded;",
                ]
            )
            if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_STRING:
                lines.extend(
                    [
                        f"      const auto bytes = {_bytes_expression(field, 'item')};",
                        "      if (!aster::proto::detail::ValidUtf8(bytes))",
                        "        return aster::Status::kInvalidArgument;",
                    ]
                )
            else:
                lines.append(f"      const auto bytes = {_bytes_expression(field, 'item')};")
            lines.extend(
                [
                    f"      if (!writer.LengthDelimited({number}, bytes))",
                    "        return aster::Status::kCapacityExceeded;",
                    "    }",
                ]
            )
        else:
            dependency = messages[field.type_name.lstrip(".")]
            encode = _codec_name(dependency.full_name, "Encode")
            lines.extend(
                [
                    f"    for (const auto& item : {member}) {{",
                    "      std::array<std::byte, "
                    f"::{dependency.full_name.replace('.', '::')}::kMaxWireSize> "
                    "nested{};",
                    "      std::size_t nested_size{};",
                    f"      const auto status = {encode}(item, nested, nested_size);",
                    "      if (!aster::IsOk(status)) return status;",
                    f"      if (!writer.LengthDelimited({number},",
                    "                                  {nested.data(), nested_size}))",
                    "        return aster::Status::kCapacityExceeded;",
                    "    }",
                ]
            )
        lines.append("  }")
        return lines

    expression = f"(*{member})" if _has_presence(field) else member
    condition = f"{member}.has_value()" if _has_presence(field) else ""
    if field.type in _PACKABLE_TYPES:
        if not condition:
            condition = _non_default(field, expression)
        lines.extend(
            [
                f"  if ({condition}) {{",
                f"    if (!writer.Tag({number}, {wire_type}) ||",
                f"        !{_scalar_writer(field, 'writer', expression)})",
                "      return aster::Status::kCapacityExceeded;",
                "  }",
            ]
        )
        return lines
    if field.type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
        descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
    }:
        if not condition:
            condition = f"{expression}.size != 0"
        lines.append(f"  if ({condition}) {{")
        lines.extend(
            [
                f"    if ({expression}.size > {bound['max_size']})",
                "      return aster::Status::kCapacityExceeded;",
            ]
        )
        lines.append(f"    const auto bytes = {_bytes_expression(field, expression)};")
        if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_STRING:
            lines.extend(
                [
                    "    if (!aster::proto::detail::ValidUtf8(bytes))",
                    "      return aster::Status::kInvalidArgument;",
                ]
            )
        lines.extend(
            [
                f"    if (!writer.LengthDelimited({number}, bytes))",
                "      return aster::Status::kCapacityExceeded;",
                "  }",
            ]
        )
        return lines
    dependency = messages[field.type_name.lstrip(".")]
    encode = _codec_name(dependency.full_name, "Encode")
    if condition:
        lines.append(f"  if ({condition}) {{")
        indent = "  "
    else:
        indent = ""
    lines.extend(
        [
            f"  {indent}std::array<std::byte, "
            f"::{dependency.full_name.replace('.', '::')}::kMaxWireSize> "
            f"{field.name}_nested{{}};",
            f"  {indent}std::size_t {field.name}_size{{}};",
            f"  {indent}const auto {field.name}_status = {encode}("
            f"{expression}, {field.name}_nested, {field.name}_size);",
            f"  {indent}if (!aster::IsOk({field.name}_status)) return {field.name}_status;",
        ]
    )
    if condition:
        lines.extend(
            [
                f"    if (!writer.LengthDelimited({number},",
                f"                                {{{field.name}_nested.data(), "
                f"{field.name}_size}}))",
                "      return aster::Status::kCapacityExceeded;",
                "  }",
            ]
        )
    else:
        lines.extend(
            [
                f"  if ({field.name}_size != 0 &&",
                f"      !writer.LengthDelimited({number},",
                f"                              {{{field.name}_nested.data(), "
                f"{field.name}_size}}))",
                "    return aster::Status::kCapacityExceeded;",
            ]
        )
    return lines


def _decoded_scalar(field: Any, raw: str) -> str:
    field_type = field.type
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_BOOL:
        return f"{raw} != 0"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_INT32:
        return f"std::bit_cast<std::int32_t>(static_cast<std::uint32_t>({raw}))"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_INT64:
        return f"std::bit_cast<std::int64_t>({raw})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_UINT32:
        return f"static_cast<std::uint32_t>({raw})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_UINT64:
        return raw
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SINT32:
        return f"aster::proto::detail::UnZigZag32(static_cast<std::uint32_t>({raw}))"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SINT64:
        return f"aster::proto::detail::UnZigZag64({raw})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_ENUM:
        return (
            f"static_cast<{_cpp_name(field.type_name)}>(std::bit_cast<std::int32_t>("
            f"static_cast<std::uint32_t>({raw})))"
        )
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_FIXED32:
        return raw
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_FIXED64:
        return raw
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED32:
        return f"std::bit_cast<std::int32_t>({raw})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_SFIXED64:
        return f"std::bit_cast<std::int64_t>({raw})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_FLOAT:
        return f"std::bit_cast<float>({raw})"
    if field_type == descriptor_pb2.FieldDescriptorProto.TYPE_DOUBLE:
        return f"std::bit_cast<double>({raw})"
    raise ProtobufProfileError("unsupported scalar protobuf type")


def _scalar_read(field: Any, reader: str, target: str, indent: str) -> list[str]:
    if field.type in _VARINT_TYPES:
        raw_type = "std::uint64_t"
        method = "Varint"
    elif field.type in _FIXED32_TYPES:
        raw_type = "std::uint32_t"
        method = "Fixed32"
    else:
        raw_type = "std::uint64_t"
        method = "Fixed64"
    return [
        f"{indent}{raw_type} raw{{}};",
        f"{indent}if (!{reader}.{method}(raw)) return aster::Status::kProtocolError;",
        f"{indent}{target} = {_decoded_scalar(field, 'raw')};",
    ]


def _skip_or_reject(indent: str = "        ") -> list[str]:
    return [
        f"{indent}if (!reader.Skip(field_number, wire_type))",
        f"{indent}  return aster::Status::kProtocolError;",
        f"{indent}break;",
    ]


def _decode_field(
    info: _Message,
    field: Any,
    bounds: dict[str, dict[str, int]],
    messages: dict[str, _Message],
) -> list[str]:
    name = f"{info.full_name}.{field.name}"
    bound = bounds.get(name, {})
    member = f"output.{field.name}"
    expected_wire = _wire_type(field)
    lines = [f"      case {field.number}: {{"]
    if field.label == descriptor_pb2.FieldDescriptorProto.LABEL_REPEATED:
        capacity = bound["max_count"]
        if field.type in _PACKABLE_TYPES:
            lines.extend(
                [
                    "        if (wire_type == 2) {",
                    "          std::span<const std::byte> packed;",
                    "          if (!reader.Bytes(packed)) return aster::Status::kProtocolError;",
                    "          aster::proto::detail::Reader packed_reader(packed);",
                    "          while (!packed_reader.empty()) {",
                    f"            if ({member}.size == {capacity})",
                    "              return aster::Status::kCapacityExceeded;",
                ]
            )
            lines.extend(_scalar_read(field, "packed_reader", "auto decoded", "            "))
            lines.extend(
                [
                    f"            {member}.push_back(decoded);",
                    "          }",
                    f"        }} else if (wire_type == {expected_wire}) {{",
                    f"          if ({member}.size == {capacity})",
                    "            return aster::Status::kCapacityExceeded;",
                ]
            )
            lines.extend(_scalar_read(field, "reader", "auto decoded", "          "))
            lines.extend(
                [
                    f"          {member}.push_back(decoded);",
                    "        } else {",
                    "          if (!reader.Skip(field_number, wire_type))",
                    "            return aster::Status::kProtocolError;",
                    "        }",
                ]
            )
        elif field.type in {
            descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
            descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
        }:
            item_type = _field_type(field, bound, name)
            item_type = item_type.removeprefix("aster::proto::BoundedVector<").rsplit(", ", 1)[0]
            lines.extend(
                [
                    f"        if (wire_type != {expected_wire}) {{",
                    "          if (!reader.Skip(field_number, wire_type))",
                    "            return aster::Status::kProtocolError;",
                    "          break;",
                    "        }",
                    f"        if ({member}.size == {capacity})",
                    "          return aster::Status::kCapacityExceeded;",
                    "        std::span<const std::byte> bytes;",
                    "        if (!reader.Bytes(bytes)) return aster::Status::kProtocolError;",
                    f"        {item_type} decoded;",
                ]
            )
            if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_STRING:
                lines.extend(
                    [
                        "        if (!aster::proto::detail::ValidUtf8(bytes))",
                        "          return aster::Status::kProtocolError;",
                        "        if (!decoded.assign({reinterpret_cast<const char*>("
                        "bytes.data()), bytes.size()}))",
                        "          return aster::Status::kCapacityExceeded;",
                    ]
                )
            else:
                lines.extend(
                    [
                        "        for (const auto byte : bytes) {",
                        "          if (!decoded.push_back(byte)) "
                        "return aster::Status::kCapacityExceeded;",
                        "        }",
                    ]
                )
            lines.append(f"        {member}.push_back(decoded);")
        else:
            dependency = messages[field.type_name.lstrip(".")]
            decode = _codec_name(dependency.full_name, "Decode")
            lines.extend(
                [
                    f"        if (wire_type != {expected_wire}) {{",
                    "          if (!reader.Skip(field_number, wire_type))",
                    "            return aster::Status::kProtocolError;",
                    "          break;",
                    "        }",
                    f"        if ({member}.size == {capacity})",
                    "          return aster::Status::kCapacityExceeded;",
                    "        std::span<const std::byte> bytes;",
                    "        if (!reader.Bytes(bytes)) return aster::Status::kProtocolError;",
                    f"        {_cpp_name(field.type_name)} decoded;",
                    f"        const auto status = {decode}(bytes, decoded);",
                    "        if (!aster::IsOk(status)) return status;",
                    f"        {member}.push_back(decoded);",
                ]
            )
        lines.extend(["        break;", "      }"])
        return lines

    if field.type in _PACKABLE_TYPES:
        lines.append(f"        if (wire_type != {expected_wire}) {{")
        lines.extend(_skip_or_reject("          "))
        lines.append("        }")
        target = "auto decoded" if _has_presence(field) else member
        lines.extend(_scalar_read(field, "reader", target, "        "))
        if _has_presence(field):
            lines.append(f"        {member} = decoded;")
    elif field.type in {
        descriptor_pb2.FieldDescriptorProto.TYPE_STRING,
        descriptor_pb2.FieldDescriptorProto.TYPE_BYTES,
    }:
        lines.extend(
            [
                f"        if (wire_type != {expected_wire}) {{",
                "          if (!reader.Skip(field_number, wire_type))",
                "            return aster::Status::kProtocolError;",
                "          break;",
                "        }",
                "        std::span<const std::byte> bytes;",
                "        if (!reader.Bytes(bytes)) return aster::Status::kProtocolError;",
            ]
        )
        target = "decoded" if _has_presence(field) else member
        if _has_presence(field):
            item_type = _field_type(field, bound, name).removeprefix("std::optional<")[:-1]
            lines.append(f"        {item_type} decoded;")
        if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_STRING:
            lines.extend(
                [
                    "        if (!aster::proto::detail::ValidUtf8(bytes))",
                    "          return aster::Status::kProtocolError;",
                    f"        if (!{target}.assign({{reinterpret_cast<const char*>("
                    "bytes.data()), bytes.size()}))",
                    "          return aster::Status::kCapacityExceeded;",
                ]
            )
        else:
            if not _has_presence(field):
                lines.append(f"        {target}.clear();")
            lines.extend(
                [
                    "        for (const auto byte : bytes) {",
                    f"          if (!{target}.push_back(byte)) "
                    "return aster::Status::kCapacityExceeded;",
                    "        }",
                ]
            )
        if _has_presence(field):
            lines.append(f"        {member} = decoded;")
    else:
        dependency = messages[field.type_name.lstrip(".")]
        decode = _codec_name(dependency.full_name, "Decode")
        lines.extend(
            [
                f"        if (wire_type != {expected_wire}) {{",
                "          if (!reader.Skip(field_number, wire_type))",
                "            return aster::Status::kProtocolError;",
                "          break;",
                "        }",
                "        std::span<const std::byte> bytes;",
                "        if (!reader.Bytes(bytes)) return aster::Status::kProtocolError;",
            ]
        )
        target = "decoded" if _has_presence(field) else member
        if _has_presence(field):
            lines.append(f"        {_cpp_name(field.type_name)} decoded;")
        lines.extend(
            [
                f"        const auto status = {decode}(bytes, {target});",
                "        if (!aster::IsOk(status)) return status;",
            ]
        )
        if _has_presence(field):
            lines.append(f"        {member} = decoded;")
    lines.extend(["        break;", "      }"])
    return lines


def _codec(info: _Message, bounds: dict[str, dict[str, int]], messages: dict[str, _Message]) -> str:
    cpp_type = _cpp_name(info.full_name)
    encode_name = _codec_name(info.full_name, "Encode")
    decode_name = _codec_name(info.full_name, "Decode")
    encode_lines = [
        f"inline aster::Status {encode_name}(const {cpp_type}& value,",
        "                            std::span<std::byte> output,",
        "                            std::size_t& written) noexcept {",
        "  static_cast<void>(value);",
        "  written = 0;",
        "  aster::proto::detail::Writer writer(output);",
    ]
    for field in sorted(info.descriptor.field, key=lambda item: item.number):
        encode_lines.extend(_encode_field(info, field, bounds, messages))
    encode_lines.extend(["  written = writer.size();", "  return aster::Status::kOk;", "}"])
    decode_lines = [
        f"inline aster::Status {decode_name}(std::span<const std::byte> input,",
        f"                            {cpp_type}& output) noexcept {{",
        "  output = {};",
        "  aster::proto::detail::Reader reader(input);",
        "  while (!reader.empty()) {",
        "    std::uint32_t field_number{};",
        "    std::uint8_t wire_type{};",
        "    if (!reader.Field(field_number, wire_type))",
        "      return aster::Status::kProtocolError;",
        "    switch (field_number) {",
    ]
    for field in sorted(info.descriptor.field, key=lambda item: item.number):
        decode_lines.extend(_decode_field(info, field, bounds, messages))
    decode_lines.extend(
        [
            "      default:",
            "        if (!reader.Skip(field_number, wire_type))",
            "          return aster::Status::kProtocolError;",
            "        break;",
            "    }",
            "  }",
            "  return aster::Status::kOk;",
            "}",
        ]
    )
    return "\n".join(encode_lines + [""] + decode_lines)


def _type_support(info: _Message, schema_hash: str) -> str:
    cpp_type = _cpp_name(info.full_name)
    encode_name = _codec_name(info.full_name, "Encode")
    decode_name = _codec_name(info.full_name, "Decode")
    return f"""namespace aster {{
template <>
struct TypeSupport<{cpp_type}> {{
  static constexpr TypeDescriptor descriptor() noexcept {{
    return {{"{info.full_name}", proto::schema_{schema_hash}::kSchemaHash,
            {cpp_type}::kMaxWireSize}};
  }}
  static Status Encode(const {cpp_type}& value, std::span<std::byte> output,
                       std::size_t& written) noexcept {{
    return proto::detail::{encode_name}(value, output, written);
  }}
  static Status Decode(std::span<const std::byte> input,
                       {cpp_type}& value) noexcept {{
    return proto::detail::{decode_name}(input, value);
  }}
}};
}}  // namespace aster
"""


def _validate_services(
    descriptor: descriptor_pb2.FileDescriptorSet,
    messages: dict[str, _Message],
) -> list[_Service]:
    services: list[_Service] = []
    for file in sorted(descriptor.file, key=lambda item: item.name):
        for service in sorted(file.service, key=lambda item: item.name):
            full_name = _qualified(file.package, service.name)
            for method in service.method:
                method_name = f"{full_name}.{method.name}"
                if method.client_streaming or method.server_streaming:
                    if method.client_streaming and method.server_streaming:
                        kind = "bidirectional-streaming"
                    elif method.client_streaming:
                        kind = "client-streaming"
                    else:
                        kind = "server-streaming"
                    raise ProtobufProfileError(f"{kind} RPC is not supported: {method_name}")
                for role, type_name in (
                    ("request", method.input_type),
                    ("response", method.output_type),
                ):
                    if type_name.lstrip(".") not in messages:
                        raise ProtobufProfileError(
                            f"RPC {method_name} uses unsupported {role} type {type_name}"
                        )
            services.append(_Service(file.package, service, full_name))
    return services


def _service_declaration(info: _Service) -> str:
    methods = "\n".join(
        f"  struct {method.name} final {{}};"
        for method in sorted(info.descriptor.method, key=lambda item: item.name)
    )
    body = f"struct {info.descriptor.name} final {{"
    if methods:
        body += f"\n{methods}\n"
    return _declaration(info.package, body + "};")


def _service_type_support(info: _Service, method: Any, schema_hash: str) -> str:
    service_type = f"{_cpp_name(info.full_name)}::{method.name}"
    request_type = _cpp_name(method.input_type)
    response_type = _cpp_name(method.output_type)
    method_name = f"{info.full_name}.{method.name}"
    return f"""namespace aster {{
template <>
struct ServiceTypeSupport<{service_type}> {{
  using Request = {request_type};
  using Response = {response_type};

  static constexpr std::string_view service_full_name() noexcept {{
    return "{info.full_name}";
  }}
  static constexpr std::string_view method_full_name() noexcept {{
    return "{method_name}";
  }}
  static constexpr ServiceDescriptor descriptor() noexcept {{
    return {{method_full_name(), proto::schema_{schema_hash}::kSchemaHash,
            TypeSupport<Request>::descriptor(),
            TypeSupport<Response>::descriptor()}};
  }}
}};
}}  // namespace aster
"""


def _validate_profile(
    descriptor: descriptor_pb2.FileDescriptorSet,
    bounds: dict[str, dict[str, int]],
) -> tuple[list[_Message], dict[str, _Message]]:
    messages: list[_Message] = []
    map_types: set[str] = set()
    field_names = {
        _qualified(file.package, message.name, field.name)
        for file in descriptor.file
        for message in file.message_type
        for field in message.field
    }
    unknown = sorted(set(bounds) - field_names)
    if unknown:
        raise ProtobufProfileError(f"bounds reference unknown fields: {', '.join(unknown)}")
    any_fields = [
        _qualified(file.package, message.name, field.name)
        for file in descriptor.file
        for message in file.message_type
        for field in message.field
        if field.type_name == ".google.protobuf.Any"
    ]
    if any_fields:
        raise ProtobufProfileError(f"Any is not supported: {', '.join(sorted(any_fields))}")
    for file in descriptor.file:
        for message in file.message_type:
            for nested in message.nested_type:
                if nested.options.map_entry:
                    map_types.add(_qualified(file.package, f"{message.name}.{nested.name}"))
    for file in sorted(descriptor.file, key=lambda item: item.name):
        if file.syntax != "proto3":
            raise ProtobufProfileError(
                "only proto3 is supported; proto2 and required fields are outside "
                f"the bounded profile: {file.name or '<unknown>'}"
            )
        for message in file.message_type:
            full_name = _qualified(file.package, message.name)
            if message.nested_type:
                if any(nested.options.map_entry for nested in message.nested_type):
                    raise ProtobufProfileError(f"map fields are not supported: {full_name}")
                raise ProtobufProfileError(f"nested declarations are not supported: {full_name}")
            if message.enum_type:
                raise ProtobufProfileError(f"nested declarations are not supported: {full_name}")
            for oneof_index, _ in enumerate(message.oneof_decl):
                members = [
                    field
                    for field in message.field
                    if field.HasField("oneof_index") and field.oneof_index == oneof_index
                ]
                if len(members) != 1 or not members[0].proto3_optional:
                    raise ProtobufProfileError(f"oneof is not supported: {full_name}")
            for field in message.field:
                field_name = f"{full_name}.{field.name}"
                if field.label == descriptor_pb2.FieldDescriptorProto.LABEL_REQUIRED:
                    raise ProtobufProfileError(f"required fields are not supported: {field_name}")
                if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_GROUP:
                    raise ProtobufProfileError(f"groups are not supported: {field_name}")
                if field.type_name.lstrip(".") in map_types:
                    raise ProtobufProfileError(f"map fields are not supported: {field_name}")
                _field_type(field, bounds.get(field_name, {}), field_name)
            messages.append(_Message(file.package, file.syntax, message, full_name))
    by_name = {message.full_name: message for message in messages}
    _validate_services(descriptor, by_name)
    return messages, by_name


def _read_descriptor_set(path: str | Path) -> descriptor_pb2.FileDescriptorSet:
    source = Path(path).resolve()
    descriptor = descriptor_pb2.FileDescriptorSet()
    try:
        descriptor.ParseFromString(source.read_bytes())
    except (OSError, DecodeError) as error:
        raise ProtobufProfileError(f"cannot parse descriptor set {source}: {error}") from error
    return descriptor


def _canonical_descriptor(descriptor: descriptor_pb2.FileDescriptorSet) -> bytes:
    canonical = descriptor_pb2.FileDescriptorSet()
    for file in sorted(descriptor.file, key=lambda item: item.name):
        canonical.file.add().CopyFrom(file)
    return canonical.SerializeToString(deterministic=True)


def _resolve_message_sizes(
    descriptor: descriptor_pb2.FileDescriptorSet,
    bounds: dict[str, dict[str, int]],
) -> tuple[list[_Message], dict[str, _Message]]:
    messages, messages_by_name = _validate_profile(descriptor, bounds)
    emitted = {
        _qualified(file.package, enum.name) for file in descriptor.file for enum in file.enum_type
    }
    pending = list(messages)
    ordered: list[_Message] = []
    while pending:
        progressed = False
        for info in list(pending):
            dependencies = {
                field.type_name.lstrip(".")
                for field in info.descriptor.field
                if field.type == descriptor_pb2.FieldDescriptorProto.TYPE_MESSAGE
            }
            if not dependencies.issubset(emitted):
                continue
            info.max_wire_size = _message_max_wire_size(info, bounds, messages_by_name)
            emitted.add(info.full_name)
            ordered.append(info)
            pending.remove(info)
            progressed = True
        if not progressed:
            names = ", ".join(message.full_name for message in pending)
            raise ProtobufProfileError(f"recursive or unresolved messages are not bounded: {names}")
    return ordered, messages_by_name


def _bounded_rpc_methods(
    services: list[_Service], messages: dict[str, _Message]
) -> tuple[BoundedRpcMethod, ...]:
    methods: list[BoundedRpcMethod] = []
    for service in services:
        for method in sorted(service.descriptor.method, key=lambda item: item.name):
            request_type = method.input_type.lstrip(".")
            response_type = method.output_type.lstrip(".")
            methods.append(
                BoundedRpcMethod(
                    f"{service.full_name}.{method.name}",
                    request_type,
                    response_type,
                    messages[request_type].max_wire_size,
                    messages[response_type].max_wire_size,
                )
            )
    return tuple(methods)


def inspect_descriptor_set(
    descriptor_set: str | Path,
    bounds_path: str | Path | None = None,
) -> BoundedSchema:
    """Resolve the canonical hash and per-message maximum encoded sizes."""

    descriptor = _read_descriptor_set(descriptor_set)
    bounds = _load_bounds(bounds_path)
    messages, messages_by_name = _resolve_message_sizes(descriptor, bounds)
    services = _validate_services(descriptor, messages_by_name)
    schema_hash = hashlib.sha256(
        _canonical_descriptor(descriptor) + canonical_json(bounds)
    ).hexdigest()
    return BoundedSchema(
        schema_hash,
        tuple(BoundedMessage(item.full_name, item.max_wire_size) for item in messages),
        _bounded_rpc_methods(services, messages_by_name),
    )


def inspect_from_proto(
    proto_files: list[str | Path],
    bounds_path: str | Path | None = None,
    includes: list[str | Path] | None = None,
    protoc: str = "protoc",
) -> BoundedSchema:
    """Compile and inspect protobuf sources without producing generated C++."""

    with tempfile.TemporaryDirectory(prefix="aster-proto-") as directory:
        descriptor = create_descriptor_set(
            proto_files, Path(directory) / "schema.pb", includes, protoc
        )
        return inspect_descriptor_set(descriptor, bounds_path)


def generate_bounded_cpp(
    descriptor_set: str | Path,
    output: str | Path,
    bounds_path: str | Path | None = None,
) -> ProtobufResult:
    descriptor = _read_descriptor_set(descriptor_set)
    bounds = _load_bounds(bounds_path)
    messages, messages_by_name = _resolve_message_sizes(descriptor, bounds)
    schema_hash = hashlib.sha256(
        _canonical_descriptor(descriptor) + canonical_json(bounds)
    ).hexdigest()
    services = _validate_services(descriptor, messages_by_name)
    lines = [_support_header(schema_hash, has_services=bool(services))]
    emitted: set[str] = set()
    for file in sorted(descriptor.file, key=lambda item: item.name):
        for enum in file.enum_type:
            values = ", ".join(f"{item.name} = {item.number}" for item in enum.value)
            lines.append(
                _declaration(
                    file.package,
                    f"enum class {enum.name} : std::int32_t {{ {values} }};",
                )
            )
            emitted.add(_qualified(file.package, enum.name))
    for info in messages:
        message = info.descriptor
        body: list[str] = []
        for field in sorted(message.field, key=lambda item: item.number):
            full_name = f"{info.full_name}.{field.name}"
            field_type = _field_type(field, bounds.get(full_name, {}), full_name)
            body.append(f"  {field_type} {field.name}{{}};")
        body.append(f"  static constexpr std::size_t kMaxWireSize = {info.max_wire_size};")
        lines.append(
            _declaration(
                info.package,
                f"struct {message.name} {{\n" + "\n".join(body) + "\n};",
            )
        )
        emitted.add(info.full_name)
    for service in services:
        lines.append(_service_declaration(service))
    lines.append("namespace aster::proto::detail {")
    for info in messages:
        lines.append(_codec(info, bounds, messages_by_name))
    lines.append("}  // namespace aster::proto::detail\n")
    for info in messages:
        lines.append(_type_support(info, schema_hash))
    for service in services:
        for method in sorted(service.descriptor.method, key=lambda item: item.name):
            lines.append(_service_type_support(service, method, schema_hash))
    destination = Path(output).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    content = "\n".join(lines)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(destination)
    profiles = tuple(BoundedMessage(item.full_name, item.max_wire_size) for item in messages)
    rpc_methods = _bounded_rpc_methods(services, messages_by_name)
    declarations = tuple(
        sorted(
            {
                *(
                    _qualified(file.package, enum.name)
                    for file in descriptor.file
                    for enum in file.enum_type
                ),
                *(message.full_name for message in messages),
                *(service.full_name for service in services),
            }
        )
    )
    return ProtobufResult(
        schema_hash,
        len(messages),
        destination,
        profiles,
        rpc_methods,
        declarations,
    )


def generate_from_proto(
    proto_files: list[str | Path],
    output: str | Path,
    bounds_path: str | Path | None = None,
    includes: list[str | Path] | None = None,
    protoc: str = "protoc",
) -> ProtobufResult:
    with tempfile.TemporaryDirectory(prefix="aster-proto-") as directory:
        descriptor = create_descriptor_set(
            proto_files, Path(directory) / "schema.pb", includes, protoc
        )
        return generate_bounded_cpp(descriptor, output, bounds_path)
