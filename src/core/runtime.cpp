#include "aster/runtime.hpp"

namespace aster {

Status Runtime::Validate() noexcept {
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    const auto* module = modules_[index].module;
    if (module == nullptr) {
      RecordFailure(LifecycleSubject::kModule, LifecycleOperation::kValidation, index, {},
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }
    const auto info = module->Info();
    const auto instance_name =
        modules_[index].instance_name.empty() ? info.name : modules_[index].instance_name;
    if (info.name.empty() || info.type.empty() || info.package.empty()) {
      RecordFailure(LifecycleSubject::kModule, LifecycleOperation::kValidation, index,
                    instance_name, Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto previous_info = modules_[previous].module->Info();
      const auto previous_name = modules_[previous].instance_name.empty()
                                     ? previous_info.name
                                     : modules_[previous].instance_name;
      if (previous_name == instance_name) {
        RecordFailure(LifecycleSubject::kModule, LifecycleOperation::kValidation, index,
                      instance_name, Status::kAlreadyExists);
        return Status::kAlreadyExists;
      }
    }
  }

  for (std::size_t index = 0; index < registries_.size(); ++index) {
    if (registries_[index].registry == nullptr || registries_[index].registry->sealed()) {
      RecordFailure(LifecycleSubject::kRegistry, LifecycleOperation::kValidation, index, {},
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (registries_[previous].registry == registries_[index].registry) {
        RecordFailure(LifecycleSubject::kRegistry, LifecycleOperation::kValidation, index, {},
                      Status::kAlreadyExists);
        return Status::kAlreadyExists;
      }
    }
  }
  return Status::kOk;
}

Status Runtime::Initialize() noexcept {
  if (state_ != RuntimeState::kConstructed) {
    return Status::kInvalidState;
  }
  const auto validation = Validate();
  if (!IsOk(validation)) {
    state_ = RuntimeState::kFailed;
    return validation;
  }

  state_ = RuntimeState::kInitializing;
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    auto& slot = modules_[index];
    const auto info = slot.module->Info();
    const auto instance_name = slot.instance_name.empty() ? info.name : slot.instance_name;
    initialized_module_count_ = index + 1;
    const auto status = slot.module->Initialize(slot.core);
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kModule, LifecycleOperation::kInitialize, index,
                    instance_name, status);
      ShutdownInitializedModules();
      state_ = RuntimeState::kFailed;
      return status;
    }
  }

  for (std::size_t index = 0; index < registries_.size(); ++index) {
    const auto status = registries_[index].registry->Seal();
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kRegistry, LifecycleOperation::kSeal, index, {}, status);
      ShutdownInitializedModules();
      state_ = RuntimeState::kFailed;
      return status;
    }
  }

  state_ = RuntimeState::kInitialized;
  return Status::kOk;
}

Status Runtime::Start() noexcept {
  if (state_ != RuntimeState::kInitialized) {
    return Status::kInvalidState;
  }
  state_ = RuntimeState::kStarting;
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    const auto info = modules_[index].module->Info();
    const auto instance_name =
        modules_[index].instance_name.empty() ? info.name : modules_[index].instance_name;
    const auto status = modules_[index].module->Start();
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kModule, LifecycleOperation::kStart, index, instance_name,
                    status);
      ShutdownInitializedModules();
      state_ = RuntimeState::kFailed;
      return status;
    }
  }
  state_ = RuntimeState::kRunning;
  return Status::kOk;
}

void Runtime::Shutdown() noexcept {
  if (state_ == RuntimeState::kStopped || state_ == RuntimeState::kFailed) {
    return;
  }
  if (state_ == RuntimeState::kConstructed) {
    state_ = RuntimeState::kStopped;
    return;
  }
  state_ = RuntimeState::kShuttingDown;
  ShutdownInitializedModules();
  state_ = RuntimeState::kStopped;
}

void Runtime::ShutdownInitializedModules() noexcept {
  while (initialized_module_count_ != 0) {
    modules_[--initialized_module_count_].module->Shutdown();
  }
}

void Runtime::RecordFailure(LifecycleSubject subject, LifecycleOperation operation,
                            std::size_t index, std::string_view name, Status status) noexcept {
  failure_ = RuntimeFailure{subject, operation, index, name, status};
}

}  // namespace aster
