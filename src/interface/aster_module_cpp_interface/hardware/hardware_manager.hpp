#pragma once

#include <concepts>
#include <string_view>

#include "aster_module_c_interface/hardware/hardware_manager_base.h"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster {

class HardwareManagerRef {
 public:
  constexpr HardwareManagerRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_hardware_manager_base_t*>;
    }
  explicit HardwareManagerRef(Backend& value) noexcept : HardwareManagerRef(value.NativeHandle()) {}
  constexpr explicit HardwareManagerRef(const aster_hardware_manager_base_t* hardware) noexcept
      : abi_(hardware != nullptr && hardware->struct_size >= sizeof(*hardware) ? hardware
                                                                               : nullptr) {}

  Status Resolve(std::string_view name, std::string_view type, void*& device) const noexcept {
    device = nullptr;
    return abi_ == nullptr || abi_->resolve == nullptr
               ? Status::kUnavailable
               : FromAbiStatus(
                     abi_->resolve(abi_->impl, ToAbiString(name), ToAbiString(type), &device));
  }

  template <typename Device>
  Status Resolve(std::string_view name, Device*& device) const noexcept
    requires requires {
      { Device::TypeName() } -> std::convertible_to<std::string_view>;
    }
  {
    void* resolved{};
    const auto status = Resolve(name, Device::TypeName(), resolved);
    device = IsOk(status) ? static_cast<Device*>(resolved) : nullptr;
    return status;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  [[nodiscard]] constexpr const aster_hardware_manager_base_t* NativeHandle() const noexcept {
    return abi_;
  }

 private:
  const aster_hardware_manager_base_t* abi_{};
};

}  // namespace aster
