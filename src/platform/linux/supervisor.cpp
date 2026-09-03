#include "aster/platform/linux/supervisor.hpp"

#include <new>
#include <utility>

namespace aster::platform::linux {

Supervisor::Supervisor(CoreRef default_core, transport::DeploymentId deployment_id) noexcept
    : default_core_(default_core), deployment_id_(deployment_id) {}

Supervisor::~Supervisor() { Shutdown(); }

Status Supervisor::AddModule(Module& module) noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
  }
  try {
    modules_.push_back({&module, default_core_, module.Info().name});
  } catch (const std::bad_alloc&) {
    return Status::kCapacityExceeded;
  } catch (...) {
    return Status::kInternal;
  }
  return Status::kOk;
}

Status Supervisor::AddRegistry(Registry& registry) noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
  }
  try {
    registries_.push_back({&registry});
  } catch (const std::bad_alloc&) {
    return Status::kCapacityExceeded;
  } catch (...) {
    return Status::kInternal;
  }
  return Status::kOk;
}

Status Supervisor::LoadPlugin(std::string_view path) noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
  }
  try {
    auto plugin = std::make_unique<PluginLoader>();
    const auto status = plugin->Open(path, default_core_);
    if (!IsOk(status)) {
      return status;
    }
    const auto plugin_modules = plugin->modules();
    modules_.reserve(modules_.size() + plugin_modules.size());
    plugins_.reserve(plugins_.size() + 1);
    plugins_.push_back(std::move(plugin));
    for (const auto& module : plugin_modules) {
      modules_.push_back(module);
    }
  } catch (const std::bad_alloc&) {
    return Status::kCapacityExceeded;
  } catch (...) {
    return Status::kInternal;
  }
  return Status::kOk;
}

Status Supervisor::Start(const transport::DeploymentId& deployment_id) noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
  }
  if (deployment_id != deployment_id_) {
    return Status::kVersionMismatch;
  }
  runtime_.emplace(std::span<ModuleSlot>(modules_), std::span<RegistrySlot>(registries_));
  auto status = runtime_->Initialize();
  if (IsOk(status)) {
    status = runtime_->Start();
  }
  state_ = IsOk(status) ? SupervisorState::kRunning : SupervisorState::kFailed;
  return status;
}

void Supervisor::Shutdown() noexcept {
  if (runtime_.has_value()) {
    runtime_->Shutdown();
  }
  if (state_ != SupervisorState::kComposing) {
    state_ = SupervisorState::kStopped;
  }
}

Status Supervisor::VisitGraph(GraphVisitor visitor, void* state) const noexcept {
  if (visitor == nullptr) {
    return Status::kInvalidArgument;
  }
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    const auto status = visitor(state, GraphModuleView{index, modules_[index].module->Info()});
    if (!IsOk(status)) {
      return status;
    }
  }
  return Status::kOk;
}

const RuntimeFailure* Supervisor::failure() const noexcept {
  if (!runtime_.has_value() || !runtime_->failure().has_value()) {
    return nullptr;
  }
  return &*runtime_->failure();
}

}  // namespace aster::platform::linux
