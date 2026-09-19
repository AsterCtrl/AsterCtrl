#pragma once

#include "aster_module_cpp_interface/core_ref.hpp"
#include "aster_runtime/core/configurator.hpp"

namespace aster {

struct CoreHandles {
  ConfiguratorRef configurator{};
  LoggerRef logger{};
  ExecutorRef executor{};
  ChannelRef channel{};
  RpcRef rpc{};
  ParameterRef parameter{};
  ClockRef clock{};
  AllocatorRef allocator{};
  HardwareManagerRef hardware{};
  std::string_view instance_name{};
};

// Owns the root C table and borrows Runtime-owned service tables. Static and
// dynamic Modules use the same canonical interface.
class CoreAdapter final {
 public:
  explicit CoreAdapter(CoreHandles handles) noexcept;

  CoreAdapter(const CoreAdapter&) = delete;
  CoreAdapter& operator=(const CoreAdapter&) = delete;
  CoreAdapter(CoreAdapter&&) = delete;
  CoreAdapter& operator=(CoreAdapter&&) = delete;

  [[nodiscard]] CoreRef ref() const noexcept { return CoreRef(&core_); }
  [[nodiscard]] const aster_core_base_t* NativeHandle() const noexcept { return &core_; }

 private:
  static const aster_configurator_base_t* GetConfigurator(void* context) noexcept;
  static const aster_logger_base_t* GetLogger(void* context) noexcept;
  static const aster_executor_base_t* GetExecutor(void* context) noexcept;
  static const aster_channel_base_t* GetChannel(void* context) noexcept;
  static const aster_rpc_base_t* GetRpc(void* context) noexcept;
  static const aster_parameter_base_t* GetParameter(void* context) noexcept;
  static const aster_clock_base_t* GetClock(void* context) noexcept;
  static const aster_allocator_base_t* GetAllocator(void* context) noexcept;
  static const aster_hardware_manager_base_t* GetHardware(void* context) noexcept;
  static aster_string_view_t GetInstanceName(void* context) noexcept;

  CoreHandles handles_;
  aster_core_base_t core_;
};

// Overrides only the per-Instance Configurator. The remaining typed Core
// accessors delegate to the resolved node CoreRef.
class CoreRefOverlay final {
 public:
  CoreRefOverlay(CoreRef fallback, Configurator& configurator,
                 std::string_view instance_name = {}) noexcept;

  CoreRefOverlay(const CoreRefOverlay&) = delete;
  CoreRefOverlay& operator=(const CoreRefOverlay&) = delete;
  CoreRefOverlay(CoreRefOverlay&&) = delete;
  CoreRefOverlay& operator=(CoreRefOverlay&&) = delete;

  [[nodiscard]] CoreRef ref() const noexcept { return CoreRef(&core_); }

 private:
  static const aster_configurator_base_t* GetConfigurator(void* context) noexcept;
  static const aster_logger_base_t* GetLogger(void* context) noexcept;
  static const aster_executor_base_t* GetExecutor(void* context) noexcept;
  static const aster_channel_base_t* GetChannel(void* context) noexcept;
  static const aster_rpc_base_t* GetRpc(void* context) noexcept;
  static const aster_parameter_base_t* GetParameter(void* context) noexcept;
  static const aster_clock_base_t* GetClock(void* context) noexcept;
  static const aster_allocator_base_t* GetAllocator(void* context) noexcept;
  static const aster_hardware_manager_base_t* GetHardware(void* context) noexcept;
  static aster_string_view_t GetInstanceName(void* context) noexcept;

  CoreRef fallback_;
  Configurator* configurator_;
  std::string_view instance_name_;
  aster_core_base_t core_;
};

}  // namespace aster
