#include "xrobot/runtime/runtime.hpp"

namespace xrobot::runtime {

Status Runtime::ValidateSlots() noexcept {
  for (std::size_t index = 0; index < executors_.size(); ++index) {
    const auto* executor = executors_[index].executor;
    if (executor == nullptr || executor->Name().empty() ||
        executor->context().executor_name() != executor->Name()) {
      RecordFailure(LifecycleSubject::kExecutor, index,
                    LifecycleOperation::kValidation,
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (executors_[previous].executor->Name() == executor->Name()) {
        RecordFailure(LifecycleSubject::kExecutor, index,
                      LifecycleOperation::kValidation,
                      Status::kInvalidArgument);
        return Status::kInvalidArgument;
      }
    }
  }

  for (std::size_t index = 0; index < modules_.size(); ++index) {
    const auto& slot = modules_[index];
    if (slot.module == nullptr || slot.context == nullptr ||
        slot.module->Name().empty() || slot.context->node_name().empty() ||
        slot.context->module_name() != slot.module->Name()) {
      RecordFailure(LifecycleSubject::kModule, index,
                    LifecycleOperation::kValidation,
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }

    bool executor_found = slot.context->executor() == nullptr;
    if (!executors_.empty()) {
      executor_found = false;
      for (const auto& executor_slot : executors_) {
        if (executor_slot.executor == slot.context->executor()) {
          executor_found = true;
          break;
        }
      }
    }
    if (!executor_found ||
        (executors_.empty() && slot.context->executor() != nullptr)) {
      RecordFailure(LifecycleSubject::kModule, index,
                    LifecycleOperation::kValidation,
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }

    for (std::size_t previous = 0; previous < index; ++previous) {
      if (modules_[previous].context->module_name() ==
          slot.context->module_name()) {
        RecordFailure(LifecycleSubject::kModule, index,
                      LifecycleOperation::kValidation,
                      Status::kInvalidArgument);
        return Status::kInvalidArgument;
      }
    }
  }

  if (scheduler_ != nullptr) {
    if (scheduler_->Name().empty() || scheduler_->task_count() == 0) {
      RecordFailure(LifecycleSubject::kScheduler, 0,
                    LifecycleOperation::kValidation,
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < scheduler_->task_count(); ++index) {
      const auto task = scheduler_->task_descriptor(index);
      bool module_found = false;
      for (const auto& module_slot : modules_) {
        if (module_slot.module->Name() == task.module_name) {
          module_found = true;
          break;
        }
      }
      bool executor_found = false;
      for (const auto& executor_slot : executors_) {
        if (executor_slot.executor == task.executor) {
          executor_found = true;
          break;
        }
      }
      if (task.module_name.empty() || task.task_name.empty() ||
          task.period_ns == 0 || !module_found || !executor_found) {
        RecordFailure(LifecycleSubject::kScheduler, index,
                      LifecycleOperation::kValidation,
                      Status::kInvalidArgument);
        return Status::kInvalidArgument;
      }
    }
  }
  return Status::kOk;
}

Status Runtime::Initialize() noexcept {
  if (state_ != RuntimeState::kConstructed) {
    return Status::kInvalidState;
  }
  if (const auto status = ValidateSlots(); !IsOk(status)) {
    state_ = RuntimeState::kFailed;
    return status;
  }

  state_ = RuntimeState::kInitializing;
  for (std::size_t index = 0; index < executors_.size(); ++index) {
    const auto status = executors_[index].executor->Initialize();
    initialized_executor_count_ = index + 1;
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kExecutor, index,
                    LifecycleOperation::kInitialize, status);
      ShutdownInitializedExecutors();
      state_ = RuntimeState::kFailed;
      return status;
    }
  }

  for (std::size_t index = 0; index < modules_.size(); ++index) {
    auto& slot = modules_[index];
    slot.context->SetPhase(ModulePhase::kInitializing);
    const auto status = slot.module->Initialize(*slot.context);
    initialized_module_count_ = index + 1;
    if (!IsOk(status)) {
      slot.context->SetPhase(ModulePhase::kFailed);
      RecordFailure(LifecycleSubject::kModule, index,
                    LifecycleOperation::kInitialize, status);
      ShutdownInitializedModules();
      ShutdownInitializedExecutors();
      state_ = RuntimeState::kFailed;
      return status;
    }
    slot.context->SetPhase(ModulePhase::kInitialized);
  }

  if (scheduler_ != nullptr) {
    const auto status = scheduler_->Initialize();
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kScheduler, 0,
                    LifecycleOperation::kInitialize, status);
      ShutdownInitializedModules();
      ShutdownInitializedExecutors();
      state_ = RuntimeState::kFailed;
      return status;
    }
    scheduler_initialized_ = true;
  }

  state_ = RuntimeState::kInitialized;
  return Status::kOk;
}

Status Runtime::Start() noexcept {
  if (state_ != RuntimeState::kInitialized) {
    return Status::kInvalidState;
  }

  state_ = RuntimeState::kStarting;
  for (std::size_t index = 0; index < executors_.size(); ++index) {
    const auto status = executors_[index].executor->Start();
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kExecutor, index,
                    LifecycleOperation::kStart, status);
      ShutdownInitializedModules();
      ShutdownInitializedExecutors();
      state_ = RuntimeState::kFailed;
      return status;
    }
  }

  for (std::size_t index = 0; index < modules_.size(); ++index) {
    auto& slot = modules_[index];
    slot.context->SetPhase(ModulePhase::kStarting);
    const auto status = slot.module->Start();
    if (!IsOk(status)) {
      slot.context->SetPhase(ModulePhase::kFailed);
      RecordFailure(LifecycleSubject::kModule, index,
                    LifecycleOperation::kStart, status);
      ShutdownInitializedModules();
      ShutdownInitializedExecutors();
      state_ = RuntimeState::kFailed;
      return status;
    }
    slot.context->SetPhase(ModulePhase::kRunning);
  }

  if (scheduler_ != nullptr) {
    const auto status = scheduler_->Start();
    if (!IsOk(status)) {
      RecordFailure(LifecycleSubject::kScheduler, 0,
                    LifecycleOperation::kStart, status);
      ShutdownInitializedScheduler();
      ShutdownInitializedModules();
      ShutdownInitializedExecutors();
      state_ = RuntimeState::kFailed;
      return status;
    }
  }

  state_ = RuntimeState::kRunning;
  return Status::kOk;
}

Status Runtime::Poll(std::uint64_t now_ns,
                     const ExecutionContext& caller) noexcept {
  if (state_ != RuntimeState::kRunning) {
    return Status::kInvalidState;
  }
  if (scheduler_ == nullptr) {
    return Status::kUnavailable;
  }
  return scheduler_->Poll(now_ns, caller);
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
  ShutdownInitializedScheduler();
  ShutdownInitializedModules();
  ShutdownInitializedExecutors();
  state_ = RuntimeState::kStopped;
}

void Runtime::ShutdownInitializedScheduler() noexcept {
  if (scheduler_initialized_) {
    scheduler_->Shutdown();
    scheduler_initialized_ = false;
  }
}

void Runtime::ShutdownInitializedModules() noexcept {
  while (initialized_module_count_ > 0) {
    auto& slot = modules_[--initialized_module_count_];
    slot.context->SetPhase(ModulePhase::kShuttingDown);
    slot.module->Shutdown();
    slot.context->SetPhase(ModulePhase::kStopped);
  }
}

void Runtime::ShutdownInitializedExecutors() noexcept {
  while (initialized_executor_count_ > 0) {
    executors_[--initialized_executor_count_].executor->Shutdown();
  }
}

void Runtime::RecordFailure(LifecycleSubject subject, std::size_t index,
                            LifecycleOperation operation,
                            Status status) noexcept {
  std::string_view name;
  if (subject == LifecycleSubject::kExecutor) {
    if (index < executors_.size() && executors_[index].executor != nullptr) {
      name = executors_[index].executor->Name();
    }
  } else if (subject == LifecycleSubject::kModule && index < modules_.size() &&
             modules_[index].module != nullptr) {
    name = modules_[index].module->Name();
  } else if (subject == LifecycleSubject::kScheduler && scheduler_ != nullptr) {
    name = scheduler_->Name();
  }
  failure_ = RuntimeFailure{subject, index, name, operation, status};
}

}  // namespace xrobot::runtime
