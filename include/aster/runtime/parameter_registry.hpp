#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "aster/runtime/parameter.hpp"
#include "aster/runtime/status.hpp"

namespace aster::runtime {

enum class ParameterType : std::uint8_t {
  kBool,
  kInt8,
  kUint8,
  kInt16,
  kUint16,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kFloat32,
  kFloat64,
};

template <typename Value>
concept RegistryParameterValue =
    std::same_as<Value, bool> || std::same_as<Value, std::int8_t> ||
    std::same_as<Value, std::uint8_t> ||
    std::same_as<Value, std::int16_t> ||
    std::same_as<Value, std::uint16_t> ||
    std::same_as<Value, std::int32_t> ||
    std::same_as<Value, std::uint32_t> ||
    std::same_as<Value, std::int64_t> ||
    std::same_as<Value, std::uint64_t> || std::same_as<Value, float> ||
    std::same_as<Value, double>;

template <RegistryParameterValue Value>
consteval ParameterType ParameterTypeOf() noexcept {
  if constexpr (std::same_as<Value, bool>) {
    return ParameterType::kBool;
  } else if constexpr (std::same_as<Value, std::int8_t>) {
    return ParameterType::kInt8;
  } else if constexpr (std::same_as<Value, std::uint8_t>) {
    return ParameterType::kUint8;
  } else if constexpr (std::same_as<Value, std::int16_t>) {
    return ParameterType::kInt16;
  } else if constexpr (std::same_as<Value, std::uint16_t>) {
    return ParameterType::kUint16;
  } else if constexpr (std::same_as<Value, std::int32_t>) {
    return ParameterType::kInt32;
  } else if constexpr (std::same_as<Value, std::uint32_t>) {
    return ParameterType::kUint32;
  } else if constexpr (std::same_as<Value, std::int64_t>) {
    return ParameterType::kInt64;
  } else if constexpr (std::same_as<Value, std::uint64_t>) {
    return ParameterType::kUint64;
  } else if constexpr (std::same_as<Value, float>) {
    return ParameterType::kFloat32;
  } else {
    return ParameterType::kFloat64;
  }
}

class ParameterResolver {
 public:
  virtual ~ParameterResolver() = default;
  virtual Status Resolve(std::string_view name, ParameterType type,
                         void*& parameter) const noexcept = 0;
};

template <std::size_t Capacity>
class StaticParameterRegistry final : public ParameterResolver {
 public:
  static_assert(Capacity > 0);

  template <RegistryParameterValue Value>
  Status Add(Parameter<Value>& parameter) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    const auto name = parameter.descriptor().name;
    if (name.empty()) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name) {
        return Status::kInvalidArgument;
      }
    }
    if (size_ == Capacity) {
      return Status::kCapacityExceeded;
    }
    entries_[size_++] =
        Entry{name, ParameterTypeOf<Value>(), static_cast<void*>(&parameter)};
    return Status::kOk;
  }

  Status Seal() noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  Status Resolve(std::string_view name, ParameterType type,
                 void*& parameter) const noexcept override {
    parameter = nullptr;
    if (!sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name) {
        if (entries_[index].type != type) {
          return Status::kTypeMismatch;
        }
        parameter = entries_[index].parameter;
        return Status::kOk;
      }
    }
    return Status::kUnavailable;
  }

 private:
  struct Entry {
    std::string_view name;
    ParameterType type{ParameterType::kBool};
    void* parameter{};
  };

  std::array<Entry, Capacity> entries_{};
  std::size_t size_{};
  bool sealed_{};
};

}  // namespace aster::runtime
