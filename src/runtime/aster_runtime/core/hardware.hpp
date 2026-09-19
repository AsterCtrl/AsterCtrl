#pragma once

#include "aster_module_cpp_interface/hardware/hardware_manager.hpp"

namespace aster {

class HardwareManager {
 public:
  HardwareManager() noexcept;
  HardwareManager(const HardwareManager&) = delete;
  HardwareManager& operator=(const HardwareManager&) = delete;
  [[nodiscard]] const aster_hardware_manager_base_t* NativeHandle() const noexcept {
    return &native_;
  }
  virtual ~HardwareManager() = default;
  virtual Status Resolve(std::string_view name, std::string_view type, void*& device) noexcept = 0;

 private:
  static aster_status_t HardwareResolve(void* context, aster_string_view_t name,
                                        aster_string_view_t type, void** device) noexcept;
  aster_hardware_manager_base_t native_;
};

}  // namespace aster
