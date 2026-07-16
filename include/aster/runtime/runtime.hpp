#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "aster/runtime/executor.hpp"
#include "aster/runtime/module.hpp"
#include "aster/runtime/periodic_scheduler.hpp"
#include "aster/runtime/status.hpp"

namespace aster::runtime {

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
  kScheduler,
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
  Runtime(std::span<ExecutorSlot> executors, std::span<ModuleSlot> modules,
          PeriodicScheduler& scheduler) noexcept
      : executors_(executors), modules_(modules), scheduler_(&scheduler) {}

  Status Initialize() noexcept;
  Status Start() noexcept;
  Status Poll(std::uint64_t now_ns,
              const ExecutionContext& caller) noexcept;
  void Shutdown() noexcept;

  RuntimeState state() const noexcept { return state_; }
  const std::optional<RuntimeFailure>& failure() const noexcept { return failure_; }

 private:
  Status ValidateSlots() noexcept;
  void ShutdownInitializedModules() noexcept;
  void ShutdownInitializedScheduler() noexcept;
  void ShutdownInitializedExecutors() noexcept;
  void RecordFailure(LifecycleSubject subject, std::size_t index,
                     LifecycleOperation operation, Status status) noexcept;

  std::span<ExecutorSlot> executors_;
  std::span<ModuleSlot> modules_;
  PeriodicScheduler* scheduler_{};
  std::size_t initialized_executor_count_{};
  std::size_t initialized_module_count_{};
  bool scheduler_initialized_{};
  RuntimeState state_{RuntimeState::kConstructed};
  std::optional<RuntimeFailure> failure_;
};

}  // namespace aster::runtime
