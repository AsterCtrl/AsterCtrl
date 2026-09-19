#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

#include "aster_module_c_interface/util/type_support_base.h"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster {

struct SchemaHash {
  std::array<std::byte, 16> bytes{};

  constexpr bool operator==(const SchemaHash&) const noexcept = default;
};

struct TypeDescriptor {
  std::string_view name;
  SchemaHash schema_hash;
  std::size_t max_serialized_size{};
};

[[nodiscard]] constexpr aster_schema_hash_t ToAbiSchemaHash(const SchemaHash& hash) noexcept {
  aster_schema_hash_t result{};
  for (std::size_t index = 0; index < hash.bytes.size(); ++index) {
    result.bytes[index] = static_cast<std::uint8_t>(hash.bytes[index]);
  }
  return result;
}

[[nodiscard]] constexpr SchemaHash FromAbiSchemaHash(const aster_schema_hash_t& hash) noexcept {
  SchemaHash result{};
  for (std::size_t index = 0; index < result.bytes.size(); ++index) {
    result.bytes[index] = static_cast<std::byte>(hash.bytes[index]);
  }
  return result;
}

[[nodiscard]] constexpr aster_type_descriptor_t ToAbiTypeDescriptor(
    const TypeDescriptor& descriptor) noexcept {
  return {
      ToAbiString(descriptor.name),
      ToAbiSchemaHash(descriptor.schema_hash),
      descriptor.max_serialized_size,
  };
}

inline Status FromAbiTypeDescriptor(const aster_type_descriptor_t* descriptor,
                                    TypeDescriptor& result) noexcept {
  if (descriptor == nullptr) {
    return Status::kInvalidArgument;
  }
  if (descriptor->name.data == nullptr || descriptor->name.size == 0 ||
      descriptor->max_serialized_size == 0 ||
      descriptor->max_serialized_size > std::numeric_limits<std::size_t>::max()) {
    return Status::kInvalidArgument;
  }
  result = {
      FromAbiString(descriptor->name),
      FromAbiSchemaHash(descriptor->schema_hash),
      static_cast<std::size_t>(descriptor->max_serialized_size),
  };
  return Status::kOk;
}

[[nodiscard]] constexpr bool SameType(const TypeDescriptor& left,
                                      const TypeDescriptor& right) noexcept {
  return left.name == right.name && left.schema_hash == right.schema_hash &&
         left.max_serialized_size == right.max_serialized_size;
}

template <typename Message>
struct TypeSupport;

template <typename Message>
concept MessageType = std::is_default_constructible_v<Message> &&
                      requires(const Message& input, Message& output, std::span<std::byte> encoded,
                               std::span<const std::byte> serialized, std::size_t& written) {
                        { TypeSupport<Message>::descriptor() } -> std::same_as<TypeDescriptor>;
                        {
                          TypeSupport<Message>::Encode(input, encoded, written)
                        } -> std::same_as<Status>;
                        {
                          TypeSupport<Message>::Decode(serialized, output)
                        } -> std::same_as<Status>;
                      };

}  // namespace aster
