#include "aster_runtime/core_adapter.hpp"

#include "aster_runtime/core/configurator.hpp"
#include "service/validation.hpp"

namespace aster {

CoreAdapter::CoreAdapter(CoreHandles handles) noexcept
    : handles_(handles),
      core_{ASTER_ABI_VERSION,
            sizeof(aster_core_base_t),
            this,
            GetConfigurator,
            GetLogger,
            GetExecutor,
            GetChannel,
            GetRpc,
            GetParameter,
            GetClock,
            GetAllocator,
            GetHardware,
            GetInstanceName} {}

const aster_configurator_base_t* CoreAdapter::GetConfigurator(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.configurator ? self->handles_.configurator.NativeHandle()
                                                        : nullptr;
}

const aster_logger_base_t* CoreAdapter::GetLogger(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.logger ? self->handles_.logger.NativeHandle() : nullptr;
}

const aster_executor_base_t* CoreAdapter::GetExecutor(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.executor ? self->handles_.executor.NativeHandle()
                                                    : nullptr;
}

const aster_channel_base_t* CoreAdapter::GetChannel(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.channel ? self->handles_.channel.NativeHandle()
                                                   : nullptr;
}

const aster_rpc_base_t* CoreAdapter::GetRpc(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.rpc ? self->handles_.rpc.NativeHandle() : nullptr;
}

const aster_parameter_base_t* CoreAdapter::GetParameter(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.parameter ? self->handles_.parameter.NativeHandle()
                                                     : nullptr;
}

const aster_clock_base_t* CoreAdapter::GetClock(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.clock ? self->handles_.clock.NativeHandle() : nullptr;
}

const aster_allocator_base_t* CoreAdapter::GetAllocator(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.allocator ? self->handles_.allocator.NativeHandle()
                                                     : nullptr;
}

const aster_hardware_manager_base_t* CoreAdapter::GetHardware(void* context) noexcept {
  auto* self = static_cast<CoreAdapter*>(context);
  return self != nullptr && self->handles_.hardware ? self->handles_.hardware.NativeHandle()
                                                    : nullptr;
}

aster_string_view_t CoreAdapter::GetInstanceName(void* context) noexcept {
  const auto* self = static_cast<CoreAdapter*>(context);
  return ToAbiString(self == nullptr ? std::string_view{} : self->handles_.instance_name);
}

CoreRefOverlay::CoreRefOverlay(CoreRef fallback, Configurator& configurator,
                               std::string_view instance_name) noexcept
    : fallback_(fallback),
      configurator_(&configurator),
      instance_name_(instance_name.empty() ? fallback.instance_name() : instance_name),
      core_{ASTER_ABI_VERSION,
            sizeof(aster_core_base_t),
            this,
            GetConfigurator,
            GetLogger,
            GetExecutor,
            GetChannel,
            GetRpc,
            GetParameter,
            GetClock,
            GetAllocator,
            GetHardware,
            GetInstanceName} {}

aster_string_view_t CoreRefOverlay::GetInstanceName(void* context) noexcept {
  const auto* self = static_cast<CoreRefOverlay*>(context);
  return ToAbiString(self == nullptr ? std::string_view{} : self->instance_name_);
}

const aster_configurator_base_t* CoreRefOverlay::GetConfigurator(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  return self == nullptr ? nullptr : self->configurator_->NativeHandle();
}

const aster_logger_base_t* CoreRefOverlay::GetLogger(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->logger != nullptr ? core->logger(core->impl) : nullptr;
}

const aster_executor_base_t* CoreRefOverlay::GetExecutor(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->executor != nullptr ? core->executor(core->impl) : nullptr;
}

const aster_channel_base_t* CoreRefOverlay::GetChannel(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->channel != nullptr ? core->channel(core->impl) : nullptr;
}

const aster_rpc_base_t* CoreRefOverlay::GetRpc(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->rpc != nullptr ? core->rpc(core->impl) : nullptr;
}

const aster_parameter_base_t* CoreRefOverlay::GetParameter(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->parameter != nullptr ? core->parameter(core->impl) : nullptr;
}

const aster_clock_base_t* CoreRefOverlay::GetClock(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->clock != nullptr ? core->clock(core->impl) : nullptr;
}

const aster_allocator_base_t* CoreRefOverlay::GetAllocator(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->allocator != nullptr ? core->allocator(core->impl) : nullptr;
}

const aster_hardware_manager_base_t* CoreRefOverlay::GetHardware(void* context) noexcept {
  auto* self = static_cast<CoreRefOverlay*>(context);
  const auto* core = self == nullptr ? nullptr : self->fallback_.NativeHandle();
  return core != nullptr && core->hardware != nullptr ? core->hardware(core->impl) : nullptr;
}

}  // namespace aster
