#include "aster_runtime/core/configurator.hpp"

#include "validation.hpp"

namespace aster {
Configurator::Configurator() noexcept
    : native_{sizeof(aster_configurator_base_t), this, GetNative} {}

aster_status_t Configurator::GetNative(void* context, aster_string_view_t key,
                                       aster_value_t* output) noexcept {
  if (context == nullptr || output == nullptr || !detail::ValidView(key))
    return ASTER_STATUS_INVALID_ARGUMENT;
  *output = {};
  ValueView value;
  const auto status = static_cast<Configurator*>(context)->Get(FromAbiString(key), value);
  if (!IsOk(status)) return ToAbiStatus(status);
  if (const auto valid = value.Validate(); !IsOk(valid)) return ToAbiStatus(valid);
  *output = value.NativeValue();
  return ASTER_STATUS_OK;
}
}  // namespace aster
