#include "aster_runtime/core/hardware.hpp"

#include "validation.hpp"

namespace aster {

HardwareManager::HardwareManager() noexcept
    : native_{sizeof(aster_hardware_manager_base_t), this, HardwareResolve} {}

aster_status_t HardwareManager::HardwareResolve(void* context, aster_string_view_t name,
                                                aster_string_view_t type, void** device) noexcept {
  if (context == nullptr || device == nullptr || !detail::ValidView(name) ||
      !detail::ValidView(type)) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  *device = nullptr;
  return ToAbiStatus(static_cast<HardwareManager*>(context)->Resolve(FromAbiString(name),
                                                                     FromAbiString(type), *device));
}

}  // namespace aster
