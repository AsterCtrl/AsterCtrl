#pragma once

#include <string_view>

#include "xrobot/runtime/executor.hpp"

namespace xrobot::runtime {

enum class ModulePhase {
  kConstructed,
  kInitializing,
  kInitialized,
  kStarting,
  kRunning,
  kShuttingDown,
  kStopped,
  kFailed,
};

class ModuleContext {
 public:
  constexpr ModuleContext(std::string_view node_name,
                          std::string_view module_name) noexcept
      : node_name_(node_name), module_name_(module_name) {}

  constexpr ModuleContext(std::string_view node_name,
                          std::string_view module_name,
                          Executor& executor) noexcept
      : node_name_(node_name),
        module_name_(module_name),
        executor_(&executor) {}

  constexpr std::string_view node_name() const noexcept { return node_name_; }
  constexpr std::string_view module_name() const noexcept { return module_name_; }
  constexpr ModulePhase phase() const noexcept { return phase_; }
  constexpr Executor* executor() const noexcept { return executor_; }

 private:
  friend class Runtime;

  constexpr void SetPhase(ModulePhase phase) noexcept { phase_ = phase; }

  std::string_view node_name_;
  std::string_view module_name_;
  Executor* executor_{};
  ModulePhase phase_{ModulePhase::kConstructed};
};

}  // namespace xrobot::runtime
