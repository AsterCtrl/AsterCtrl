#include "aster_runtime/platform/linux/supervisor.hpp"

#include <new>
#include <utility>

namespace aster::platform::linux {

Supervisor::Supervisor(CoreRef default_core, transport::DeploymentId deployment_id,
                       ExecutorLifecycle executor) noexcept
    : default_core_(default_core), deployment_id_(deployment_id), executor_(executor) {}

Supervisor::~Supervisor() { Shutdown(); }

Status Supervisor::AddModule(ModuleBase& module) noexcept {
  return AddModule({ModuleRef(module), default_core_, module.Info().name});
}

Status Supervisor::AddModule(const ModuleSlot& module) noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
  }
  if (!module.module) {
    return Status::kInvalidArgument;
  }
  try {
    modules_.push_back(module);
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

Status Supervisor::LoadPackage(std::string_view path,
                               std::span<const PackageModule> modules) noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
  }
  if (modules.empty()) {
    return Status::kInvalidArgument;
  }
  try {
    auto plugin = std::make_unique<PluginLoader>();
    const auto status = plugin->Open(path);
    if (!IsOk(status)) {
      return status;
    }
    for (const auto& module : modules) {
      const auto create_status = plugin->CreateModule(module.module_name, module.instance_name,
                                                      module.core ? module.core : default_core_);
      if (!IsOk(create_status)) {
        return create_status;
      }
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

Status Supervisor::Initialize() noexcept {
  if (state_ != SupervisorState::kComposing) {
    return Status::kInvalidState;
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
  const auto status = runtime_->Initialize();
  if (!IsOk(status) && executor_prepared_) executor_.quiesce(executor_.state);
  state_ = IsOk(status) ? SupervisorState::kInitialized : SupervisorState::kFailed;
  return status;
}

Status Supervisor::Start(const transport::DeploymentId& deployment_id) noexcept {
  if (deployment_id != deployment_id_) return Status::kVersionMismatch;
  if (state_ == SupervisorState::kComposing) {
    const auto status = Initialize();
    if (!IsOk(status)) return status;
  }
  if (state_ != SupervisorState::kInitialized) return Status::kInvalidState;
  auto status = runtime_->Start();
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
    const auto& slot = modules_[index];
    const auto info = slot.module.Info();
    const auto status = visitor(
        state,
        GraphModuleView{index, info, slot.instance_name.empty() ? info.name : slot.instance_name});
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
