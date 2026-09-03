#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

#include "aster/status.hpp"

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
