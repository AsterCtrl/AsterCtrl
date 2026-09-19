#pragma once

#include <cmath>
#include <concepts>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include "aster_module_c_interface/util/value.h"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster {

template <typename T>
concept ConfigValueType =
    std::integral<T> || std::floating_point<T> || std::same_as<T, std::string_view> ||
    std::same_as<T, std::span<const std::byte>>;

/// A typed, non-owning scalar or bounded payload. See each provider's lifetime contract.
class ValueView {
 public:
  constexpr ValueView() noexcept = default;
  constexpr explicit ValueView(aster_value_t value) noexcept : value_(value) {}
  constexpr explicit ValueView(bool value) noexcept {
    value_.kind = ASTER_VALUE_BOOL;
    value_.data.boolean = value ? 1 : 0;
  }
  template <std::signed_integral T>
  constexpr explicit ValueView(T value) noexcept {
    value_.kind = ASTER_VALUE_INT64;
    value_.data.integer = value;
  }
  template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
  constexpr explicit ValueView(T value) noexcept {
    value_.kind = ASTER_VALUE_UINT64;
    value_.data.unsigned_integer = value;
  }
  template <std::floating_point T>
  constexpr explicit ValueView(T value) noexcept {
    value_.kind = ASTER_VALUE_FLOAT64;
    value_.data.real = static_cast<double>(value);
  }
  constexpr explicit ValueView(std::string_view value) noexcept {
    value_.kind = ASTER_VALUE_STRING;
    value_.data.string = ToAbiString(value);
  }
  explicit ValueView(std::span<const std::byte> value) noexcept {
    value_.kind = ASTER_VALUE_BYTES;
    value_.data.bytes = {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
  }

  [[nodiscard]] constexpr std::uint32_t kind() const noexcept { return value_.kind; }
  [[nodiscard]] constexpr const aster_value_t& NativeValue() const noexcept { return value_; }

  [[nodiscard]] Status Validate() const noexcept {
    switch (kind()) {
      case ASTER_VALUE_NULL:
        return Status::kOk;
      case ASTER_VALUE_BOOL:
        return value_.data.boolean <= 1 ? Status::kOk : Status::kInvalidArgument;
      case ASTER_VALUE_INT64:
      case ASTER_VALUE_UINT64:
        return Status::kOk;
      case ASTER_VALUE_FLOAT64:
        return std::isfinite(value_.data.real) ? Status::kOk : Status::kInvalidArgument;
      case ASTER_VALUE_STRING:
        return value_.data.string.size == 0 || value_.data.string.data != nullptr
                   ? Status::kOk
                   : Status::kInvalidArgument;
      case ASTER_VALUE_BYTES:
        return value_.data.bytes.size == 0 || value_.data.bytes.data != nullptr
                   ? Status::kOk
                   : Status::kInvalidArgument;
      default:
        return Status::kTypeMismatch;
    }
  }

  template <ConfigValueType T>
  Status Get(T& output) const noexcept {
    if (const auto status = Validate(); !IsOk(status)) return status;
    if constexpr (std::same_as<T, bool>) {
      if (kind() != ASTER_VALUE_BOOL) return Status::kTypeMismatch;
      output = value_.data.boolean != 0;
    } else if constexpr (std::integral<T>) {
      if (kind() == ASTER_VALUE_INT64) {
        if (!std::in_range<T>(value_.data.integer)) return Status::kInvalidArgument;
        output = static_cast<T>(value_.data.integer);
      } else if (kind() == ASTER_VALUE_UINT64) {
        if (!std::in_range<T>(value_.data.unsigned_integer)) return Status::kInvalidArgument;
        output = static_cast<T>(value_.data.unsigned_integer);
      } else
        return Status::kTypeMismatch;
    } else if constexpr (std::floating_point<T>) {
      if (kind() != ASTER_VALUE_FLOAT64) return Status::kTypeMismatch;
      const auto value = value_.data.real;
      if (value > std::numeric_limits<T>::max() || value < std::numeric_limits<T>::lowest())
        return Status::kInvalidArgument;
      output = static_cast<T>(value);
    } else if constexpr (std::same_as<T, std::string_view>) {
      if (kind() != ASTER_VALUE_STRING) return Status::kTypeMismatch;
      output = FromAbiString(value_.data.string);
    } else {
      if (kind() != ASTER_VALUE_BYTES) return Status::kTypeMismatch;
      output = {reinterpret_cast<const std::byte*>(value_.data.bytes.data), value_.data.bytes.size};
    }
    return Status::kOk;
  }

  /// Copy string/bytes into supplied storage; scalar values need no storage.
  Status CopyTo(std::span<std::byte> storage, ValueView& output) const noexcept {
    if (const auto status = Validate(); !IsOk(status)) return status;
    const void* data{};
    std::size_t size{};
    if (kind() == ASTER_VALUE_STRING) {
      data = value_.data.string.data;
      size = value_.data.string.size;
    } else if (kind() == ASTER_VALUE_BYTES) {
      data = value_.data.bytes.data;
      size = value_.data.bytes.size;
    } else {
      output = *this;
      return Status::kOk;
    }
    if (size > storage.size()) return Status::kCapacityExceeded;
    if (size != 0) std::memmove(storage.data(), data, size);
    output = kind() == ASTER_VALUE_STRING
                 ? ValueView(std::string_view(reinterpret_cast<const char*>(storage.data()), size))
                 : ValueView(std::span<const std::byte>(storage.data(), size));
    return Status::kOk;
  }

 private:
  aster_value_t value_{};
};

}  // namespace aster
