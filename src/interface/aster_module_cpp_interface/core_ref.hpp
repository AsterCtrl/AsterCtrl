#pragma once

#include "aster_module_c_interface/core_base.h"
#include "aster_module_cpp_interface/allocator/allocator.hpp"
#include "aster_module_cpp_interface/channel.hpp"
#include "aster_module_cpp_interface/clock/clock.hpp"
#include "aster_module_cpp_interface/configurator/configurator.hpp"
#include "aster_module_cpp_interface/executor.hpp"
#include "aster_module_cpp_interface/hardware/hardware_manager.hpp"
#include "aster_module_cpp_interface/logger/logger.hpp"
#include "aster_module_cpp_interface/parameter/parameter.hpp"
#include "aster_module_cpp_interface/rpc.hpp"

namespace aster {

class CoreRef {
 public:
  constexpr CoreRef() noexcept = default;
  constexpr explicit CoreRef(const aster_core_base_t* core) noexcept : core_(core) {}

  [[nodiscard]] std::string_view instance_name() const noexcept {
    return Valid() && core_->instance_name != nullptr
               ? FromAbiString(core_->instance_name(core_->impl))
               : std::string_view{};
  }

  [[nodiscard]] ConfiguratorRef configurator() const noexcept {
    if (!Valid() || core_->configurator == nullptr) {
      return {};
    }
    const auto* value = core_->configurator(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? ConfiguratorRef(value)
                                                                    : ConfiguratorRef{};
  }

  [[nodiscard]] LoggerRef logger() const noexcept {
    if (!Valid() || core_->logger == nullptr) {
      return {};
    }
    const auto* value = core_->logger(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? LoggerRef(value)
                                                                    : LoggerRef{};
  }

  [[nodiscard]] ExecutorRef executor() const noexcept {
    if (!Valid() || core_->executor == nullptr) {
      return {};
    }
    const auto* value = core_->executor(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? ExecutorRef(value)
                                                                    : ExecutorRef{};
  }

  [[nodiscard]] ChannelRef channel() const noexcept {
    if (!Valid() || core_->channel == nullptr) {
      return {};
    }
    const auto* value = core_->channel(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? ChannelRef(value)
                                                                    : ChannelRef{};
  }

  [[nodiscard]] RpcRef rpc() const noexcept {
    if (!Valid() || core_->rpc == nullptr) {
      return {};
    }
    const auto* value = core_->rpc(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? RpcRef(value) : RpcRef{};
  }

  [[nodiscard]] ParameterRef parameter() const noexcept {
    if (!Valid() || core_->parameter == nullptr) {
      return {};
    }
    const auto* value = core_->parameter(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? ParameterRef(value)
                                                                    : ParameterRef{};
  }

  [[nodiscard]] ClockRef clock() const noexcept {
    if (!Valid() || core_->clock == nullptr) {
      return {};
    }
    const auto* value = core_->clock(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? ClockRef(value) : ClockRef{};
  }

  [[nodiscard]] AllocatorRef allocator() const noexcept {
    if (!Valid() || core_->allocator == nullptr) {
      return {};
    }
    const auto* value = core_->allocator(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? AllocatorRef(value)
                                                                    : AllocatorRef{};
  }

  [[nodiscard]] HardwareManagerRef hardware() const noexcept {
    if (!Valid() || core_->hardware == nullptr) {
      return {};
    }
    const auto* value = core_->hardware(core_->impl);
    return value != nullptr && value->struct_size >= sizeof(*value) ? HardwareManagerRef(value)
                                                                    : HardwareManagerRef{};
  }

  [[nodiscard]] constexpr const aster_core_base_t* NativeHandle() const noexcept { return core_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return Valid(); }

 private:
  [[nodiscard]] constexpr bool Valid() const noexcept {
    return core_ != nullptr && core_->abi_version == ASTER_ABI_VERSION &&
           core_->struct_size >= sizeof(aster_core_base_t);
  }

  const aster_core_base_t* core_{};
};

}  // namespace aster
