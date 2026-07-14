#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "xrobot/runtime/module.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::runtime {

enum class RuntimeState {
  kConstructed,
  kInitializing,
  kInitialized,
  kStarting,
  kRunning,
  kShuttingDown,
  kStopped,
  kFailed,
};

enum class LifecycleOperation {
  kValidation,
  kInitialize,
  kStart,
};

struct RuntimeFailure {
  std::size_t module_index{};
  std::string_view module_name;
  LifecycleOperation operation{LifecycleOperation::kValidation};
  Status status{Status::kOk};
};

class Runtime {
 public:
  explicit Runtime(std::span<ModuleSlot> modules) noexcept : modules_(modules) {}

  Status Initialize() noexcept;
  Status Start() noexcept;
  void Shutdown() noexcept;

  RuntimeState state() const noexcept { return state_; }
  const std::optional<RuntimeFailure>& failure() const noexcept { return failure_; }

 private:
  Status ValidateSlots() noexcept;
  void ShutdownInitialized() noexcept;
  void RecordFailure(std::size_t index, LifecycleOperation operation,
                     Status status) noexcept;

  std::span<ModuleSlot> modules_;
  std::size_t initialized_count_{};
  RuntimeState state_{RuntimeState::kConstructed};
  std::optional<RuntimeFailure> failure_;
};

}  // namespace xrobot::runtime
