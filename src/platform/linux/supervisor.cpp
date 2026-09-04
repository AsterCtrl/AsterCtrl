#include "aster/platform/linux/supervisor.hpp"

#include <new>
#include <utility>

namespace aster::platform::linux {

Supervisor::Supervisor(CoreRef default_core, transport::DeploymentId deployment_id,
                       ExecutorLifecycle executor) noexcept
    : default_core_(default_core), deployment_id_(deployment_id), executor_(executor) {}

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
  const auto lifecycle_hooks = static_cast<unsigned>(executor_.prepare != nullptr) +
                               static_cast<unsigned>(executor_.activate != nullptr) +
                               static_cast<unsigned>(executor_.quiesce != nullptr);
  if (lifecycle_hooks != 0U && lifecycle_hooks != 3U) {
    return Status::kInvalidArgument;
  }
  if (executor_.prepare != nullptr) {
    const auto status = executor_.prepare(executor_.state);
    if (!IsOk(status)) {
      state_ = SupervisorState::kFailed;
      return status;
    }
    executor_prepared_ = true;
  }
  runtime_.emplace(std::span<ModuleSlot>(modules_), std::span<RegistrySlot>(registries_),
                   RuntimeHooks{executor_.quiesce, executor_.state});
  auto status = runtime_->Initialize();
  if (IsOk(status)) {
    status = runtime_->Start();
  }
  if (IsOk(status) && executor_.activate != nullptr) {
    status = executor_.activate(executor_.state);
    if (!IsOk(status)) {
      runtime_->Shutdown();
    }
  } else if (!IsOk(status) && executor_prepared_ && executor_.quiesce != nullptr) {
    executor_.quiesce(executor_.state);
  }
  state_ = IsOk(status) ? SupervisorState::kRunning : SupervisorState::kFailed;
  return status;
}

void Supervisor::Shutdown() noexcept {
  if (runtime_.has_value()) {
    runtime_->Shutdown();
  } else if (executor_prepared_ && executor_.quiesce != nullptr) {
    executor_.quiesce(executor_.state);
  }
  executor_prepared_ = false;
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
