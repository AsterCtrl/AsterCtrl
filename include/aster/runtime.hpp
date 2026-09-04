#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "aster/module.hpp"
#include "aster/registry.hpp"
#include "aster/status.hpp"

namespace aster {

enum class RuntimeState : std::uint8_t {
  kConstructed,
  kInitializing,
  kInitialized,
  kStarting,
  kRunning,
  kShuttingDown,
  kStopped,
  kFailed,
};

enum class LifecycleSubject : std::uint8_t {
  kModule,
  kRegistry,
};

enum class LifecycleOperation : std::uint8_t {
  kValidation,
  kInitialize,
  kSeal,
  kStart,
};

struct RuntimeFailure {
  LifecycleSubject subject{LifecycleSubject::kModule};
  LifecycleOperation operation{LifecycleOperation::kValidation};
  std::size_t index{};
  std::string_view name;
  Status status{Status::kOk};
};

struct RuntimeHooks {
  using BeforeModulesShutdown = void (*)(void*) noexcept;

  BeforeModulesShutdown before_modules_shutdown{};
  void* state{};
};

class Runtime {
 public:
  explicit constexpr Runtime(std::span<ModuleSlot> modules) noexcept : modules_(modules) {}
  constexpr Runtime(std::span<ModuleSlot> modules, std::span<RegistrySlot> registries) noexcept
      : modules_(modules), registries_(registries) {}
  constexpr Runtime(std::span<ModuleSlot> modules, std::span<RegistrySlot> registries,
                    RuntimeHooks hooks) noexcept
      : modules_(modules), registries_(registries), hooks_(hooks) {}

  Status Initialize() noexcept;
  Status Start() noexcept;
  void Shutdown() noexcept;

  [[nodiscard]] RuntimeState state() const noexcept { return state_; }
  [[nodiscard]] const std::optional<RuntimeFailure>& failure() const noexcept { return failure_; }

 private:
  Status Validate() noexcept;
  void ShutdownInitializedModules() noexcept;
  void RecordFailure(LifecycleSubject subject, LifecycleOperation operation, std::size_t index,
                     std::string_view name, Status status) noexcept;

  std::span<ModuleSlot> modules_;
  std::span<RegistrySlot> registries_;
  std::size_t initialized_module_count_{};
  RuntimeState state_{RuntimeState::kConstructed};
  std::optional<RuntimeFailure> failure_;
  RuntimeHooks hooks_{};
};

}  // namespace aster
