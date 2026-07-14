#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "xrobot/runtime/executor.hpp"
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

enum class LifecycleSubject {
  kExecutor,
  kModule,
};

struct RuntimeFailure {
  LifecycleSubject subject{LifecycleSubject::kModule};
  std::size_t index{};
  std::string_view name;
  LifecycleOperation operation{LifecycleOperation::kValidation};
  Status status{Status::kOk};
};

class Runtime {
 public:
  explicit Runtime(std::span<ModuleSlot> modules) noexcept : modules_(modules) {}
  Runtime(std::span<ExecutorSlot> executors,
          std::span<ModuleSlot> modules) noexcept
      : executors_(executors), modules_(modules) {}

  Status Initialize() noexcept;
  Status Start() noexcept;
  void Shutdown() noexcept;

  RuntimeState state() const noexcept { return state_; }
  const std::optional<RuntimeFailure>& failure() const noexcept { return failure_; }

 private:
  Status ValidateSlots() noexcept;
  void ShutdownInitializedModules() noexcept;
  void ShutdownInitializedExecutors() noexcept;
  void RecordFailure(LifecycleSubject subject, std::size_t index,
                     LifecycleOperation operation, Status status) noexcept;

  std::span<ExecutorSlot> executors_;
  std::span<ModuleSlot> modules_;
  std::size_t initialized_executor_count_{};
  std::size_t initialized_module_count_{};
  RuntimeState state_{RuntimeState::kConstructed};
  std::optional<RuntimeFailure> failure_;
};

}  // namespace xrobot::runtime
