#pragma once

#include <concepts>
#include <string_view>

#include "aster_module_c_interface/configurator/configurator_base.h"
#include "aster_module_cpp_interface/util/value.hpp"

namespace aster {

/// Read-only instance configuration. Borrowed strings remain valid until shutdown.
class ConfiguratorRef {
 public:
  constexpr ConfiguratorRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_configurator_base_t*>;
    }
  explicit ConfiguratorRef(Backend& value) noexcept : ConfiguratorRef(value.NativeHandle()) {}
  constexpr explicit ConfiguratorRef(const aster_configurator_base_t* value) noexcept
      : abi_(value != nullptr && value->struct_size >= sizeof(*value) ? value : nullptr) {}

  Status Get(std::string_view key, ValueView& output) const noexcept {
    if (abi_ == nullptr || abi_->get == nullptr) return Status::kUnavailable;
    aster_value_t value{};
    const auto status = FromAbiStatus(abi_->get(abi_->impl, ToAbiString(key), &value));
    if (!IsOk(status)) return status;
    ValueView result(value);
    if (const auto valid = result.Validate(); !IsOk(valid)) return valid;
    output = result;
    return Status::kOk;
  }

  template <ConfigValueType T>
  Status Get(std::string_view key, T& output) const noexcept {
    ValueView value;
    const auto status = Get(key, value);
    return IsOk(status) ? value.Get(output) : status;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }
  [[nodiscard]] constexpr const aster_configurator_base_t* NativeHandle() const noexcept {
    return abi_;
  }

 private:
  const aster_configurator_base_t* abi_{};
};
}  // namespace aster
