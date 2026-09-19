#pragma once

#include "aster_module_c_interface/parameter/parameter_base.h"
#include "aster_module_cpp_interface/execution.hpp"
#include "aster_module_cpp_interface/util/value.hpp"

namespace aster {

class ParameterRef {
 public:
  constexpr ParameterRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_parameter_base_t*>;
    }
  explicit ParameterRef(Backend& value) noexcept : ParameterRef(value.NativeHandle()) {}
  constexpr explicit ParameterRef(const aster_parameter_base_t* value) noexcept
      : abi_(value != nullptr && value->struct_size >= sizeof(*value) ? value : nullptr) {}

  Status Get(std::string_view name, ValueView& output,
             std::span<std::byte> storage = {}) const noexcept {
    if (abi_ == nullptr || abi_->get == nullptr) return Status::kUnavailable;
    aster_value_t value{};
    const auto status =
        FromAbiStatus(abi_->get(abi_->impl, ToAbiString(name), &value,
                                reinterpret_cast<std::uint8_t*>(storage.data()), storage.size()));
    if (!IsOk(status)) return status;
    ValueView result(value);
    if (const auto valid = result.Validate(); !IsOk(valid)) return valid;
    output = result;
    return Status::kOk;
  }

  template <ConfigValueType T>
  Status Get(std::string_view name, T& output, std::span<std::byte> storage = {}) const noexcept {
    ValueView value;
    const auto status = Get(name, value, storage);
    return IsOk(status) ? value.Get(output) : status;
  }

  Status Set(std::string_view name, ValueView value,
             const ExecutionContext& caller) const noexcept {
    return Set(name, value, &caller);
  }
  Status Set(std::string_view name, ValueView value,
             const ExecutionContext* caller = nullptr) const noexcept {
    if (abi_ == nullptr || abi_->set == nullptr) return Status::kUnavailable;
    const auto context =
        caller == nullptr ? aster_execution_context_t{} : ToAbiExecutionContext(*caller);
    return FromAbiStatus(abi_->set(abi_->impl, ToAbiString(name), &value.NativeValue(),
                                   caller == nullptr ? nullptr : &context));
  }
  template <ConfigValueType T>
  Status Set(std::string_view name, T value) const noexcept {
    return Set(name, ValueView(value));
  }
  template <ConfigValueType T>
  Status Set(std::string_view name, T value, const ExecutionContext& caller) const noexcept {
    return Set(name, ValueView(value), caller);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }
  [[nodiscard]] constexpr const aster_parameter_base_t* NativeHandle() const noexcept {
    return abi_;
  }

 private:
  const aster_parameter_base_t* abi_{};
};
}  // namespace aster
