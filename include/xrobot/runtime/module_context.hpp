#pragma once

#include <string_view>

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

  constexpr std::string_view node_name() const noexcept { return node_name_; }
  constexpr std::string_view module_name() const noexcept { return module_name_; }
  constexpr ModulePhase phase() const noexcept { return phase_; }

 private:
  friend class Runtime;

  constexpr void SetPhase(ModulePhase phase) noexcept { phase_ = phase; }

  std::string_view node_name_;
  std::string_view module_name_;
  ModulePhase phase_{ModulePhase::kConstructed};
};

}  // namespace xrobot::runtime
