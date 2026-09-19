#include "aster_runtime/core/parameter.hpp"

#include "aster_runtime/execution.hpp"
#include "validation.hpp"

namespace aster {
ParameterStore::ParameterStore() noexcept
    : native_{sizeof(aster_parameter_base_t), this, GetNative, SetNative} {}

aster_status_t ParameterStore::GetNative(void* context, aster_string_view_t name,
                                         aster_value_t* output, std::uint8_t* storage,
                                         std::size_t capacity) noexcept {
  if (context == nullptr || output == nullptr || !detail::ValidView(name) ||
      !detail::ValidBuffer(storage, capacity))
    return ASTER_STATUS_INVALID_ARGUMENT;
  *output = {};
  ValueView value;
  const auto status = static_cast<ParameterStore*>(context)->Get(
      FromAbiString(name), value, {reinterpret_cast<std::byte*>(storage), capacity});
  if (!IsOk(status)) return ToAbiStatus(status);
  if (const auto valid = value.Validate(); !IsOk(valid)) return ToAbiStatus(valid);
  *output = value.NativeValue();
  return ASTER_STATUS_OK;
}

aster_status_t ParameterStore::SetNative(void* context, aster_string_view_t name,
                                         const aster_value_t* value,
                                         const aster_execution_context_t* caller) noexcept {
  if (context == nullptr || value == nullptr || !detail::ValidView(name))
    return ASTER_STATUS_INVALID_ARGUMENT;
  ExecutionContext resolved("unmanaged", ExecutionKind::kThread, 0);
  if (const auto valid = ResolveCallContext(caller, resolved); !IsOk(valid))
    return ToAbiStatus(valid);
  ValueView input(*value);
  if (const auto valid = input.Validate(); !IsOk(valid)) return ToAbiStatus(valid);
  return ToAbiStatus(
      static_cast<ParameterStore*>(context)->Set(FromAbiString(name), input, resolved));
}
}  // namespace aster
