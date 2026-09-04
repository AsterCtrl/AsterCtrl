#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "aster/platform/linux/plugin_loader.hpp"
#include "aster/runtime.hpp"
#include "aster/transport/peer_registry.hpp"

namespace aster::platform::linux {

enum class SupervisorState : std::uint8_t {
  kComposing,
  kRunning,
  kStopped,
  kFailed,
};

struct ExecutorLifecycle {
  using StatusHook = Status (*)(void*) noexcept;
  using ShutdownHook = void (*)(void*) noexcept;

  StatusHook prepare{};
  StatusHook activate{};
  ShutdownHook quiesce{};
  void* state{};
};

struct GraphModuleView {
  std::size_t index{};
  ModuleInfo info;
};

using GraphVisitor = Status (*)(void*, const GraphModuleView&) noexcept;

class Supervisor {
 public:
  Supervisor(CoreRef default_core, transport::DeploymentId deployment_id,
             ExecutorLifecycle executor = {}) noexcept;
  ~Supervisor();

  Supervisor(const Supervisor&) = delete;
  Supervisor& operator=(const Supervisor&) = delete;

  Status AddModule(Module& module) noexcept;
  Status AddRegistry(Registry& registry) noexcept;
  Status LoadPlugin(std::string_view path) noexcept;
  Status Start(const transport::DeploymentId& deployment_id) noexcept;
  void Shutdown() noexcept;

  Status VisitGraph(GraphVisitor visitor, void* state) const noexcept;

  [[nodiscard]] SupervisorState state() const noexcept { return state_; }
  [[nodiscard]] std::size_t module_count() const noexcept { return modules_.size(); }
  [[nodiscard]] const RuntimeFailure* failure() const noexcept;

 private:
  CoreRef default_core_;
  transport::DeploymentId deployment_id_;
  std::vector<ModuleSlot> modules_;
  std::vector<RegistrySlot> registries_;
  std::vector<std::unique_ptr<PluginLoader>> plugins_;
  std::optional<Runtime> runtime_;
  ExecutorLifecycle executor_;
  bool executor_prepared_{};
  SupervisorState state_{SupervisorState::kComposing};
};

}  // namespace aster::platform::linux
