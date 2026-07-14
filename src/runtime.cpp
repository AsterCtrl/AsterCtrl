#include "xrobot/runtime/runtime.hpp"

namespace xrobot::runtime {

Status Runtime::ValidateSlots() noexcept {
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    const auto& slot = modules_[index];
    if (slot.module == nullptr || slot.context == nullptr ||
        slot.module->Name().empty() || slot.context->node_name().empty() ||
        slot.context->module_name() != slot.module->Name()) {
      RecordFailure(index, LifecycleOperation::kValidation,
                    Status::kInvalidArgument);
      return Status::kInvalidArgument;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (modules_[previous].context->module_name() ==
          slot.context->module_name()) {
        RecordFailure(index, LifecycleOperation::kValidation,
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
  for (std::size_t index = 0; index < modules_.size(); ++index) {
    auto& slot = modules_[index];
    slot.context->SetPhase(ModulePhase::kInitializing);
    const auto status = slot.module->Initialize(*slot.context);
    initialized_count_ = index + 1;
    if (!IsOk(status)) {
      slot.context->SetPhase(ModulePhase::kFailed);
      RecordFailure(index, LifecycleOperation::kInitialize, status);
      ShutdownInitialized();
      state_ = RuntimeState::kFailed;
      return status;
    }
    slot.context->SetPhase(ModulePhase::kInitialized);
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
    auto& slot = modules_[index];
    slot.context->SetPhase(ModulePhase::kStarting);
    const auto status = slot.module->Start();
    if (!IsOk(status)) {
      slot.context->SetPhase(ModulePhase::kFailed);
      RecordFailure(index, LifecycleOperation::kStart, status);
      ShutdownInitialized();
      state_ = RuntimeState::kFailed;
      return status;
    }
    slot.context->SetPhase(ModulePhase::kRunning);
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
  ShutdownInitialized();
  state_ = RuntimeState::kStopped;
}

void Runtime::ShutdownInitialized() noexcept {
  while (initialized_count_ > 0) {
    auto& slot = modules_[--initialized_count_];
    slot.context->SetPhase(ModulePhase::kShuttingDown);
    slot.module->Shutdown();
    slot.context->SetPhase(ModulePhase::kStopped);
  }
}

void Runtime::RecordFailure(std::size_t index,
                            LifecycleOperation operation,
                            Status status) noexcept {
  const auto name = index < modules_.size() && modules_[index].module != nullptr
                        ? modules_[index].module->Name()
                        : std::string_view{};
  failure_ = RuntimeFailure{index, name, operation, status};
}

}  // namespace xrobot::runtime
