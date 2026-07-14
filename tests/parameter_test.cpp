#include <cassert>

#include "xrobot/runtime/parameter.hpp"

namespace {

using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::Parameter;
using xrobot::runtime::ParameterDescriptor;
using xrobot::runtime::ParameterMutability;
using xrobot::runtime::ParameterPersistence;
using xrobot::runtime::ParameterWritePhase;
using xrobot::runtime::Status;

struct UpdateState {
  int calls{};
  float last_candidate{};
};

Status ValidateGain(void* state, float, float candidate, ParameterWritePhase,
                    const ExecutionContext&) noexcept {
  auto& update = *static_cast<UpdateState*>(state);
  ++update.calls;
  update.last_candidate = candidate;
  return candidate == 4.0F ? Status::kUnavailable : Status::kOk;
}

void BuildTimeParametersCannotBeMutated() {
  constexpr ParameterDescriptor<float> descriptor{
      "motor.count", "count", 4.0F, 1.0F, 8.0F,
      ParameterMutability::kBuildTime, ParameterPersistence::kCompiled};
  Parameter parameter(descriptor);
  const ExecutionContext config("config", ExecutionKind::kThread, 1);

  assert(parameter.value() == 4.0F);
  assert(parameter.Set(6.0F, ParameterWritePhase::kStartup, config) ==
         Status::kInvalidState);
}

void StartupParametersLockWhenConfigurationIsSealed() {
  constexpr ParameterDescriptor<float> descriptor{
      "controller.kp", "N/A", 1.0F, 0.0F, 10.0F,
      ParameterMutability::kStartup, ParameterPersistence::kCompiled};
  Parameter parameter(descriptor);
  const ExecutionContext config("config", ExecutionKind::kThread, 1);

  assert(parameter.Set(2.0F, ParameterWritePhase::kStartup, config) ==
         Status::kOk);
  parameter.SealStartup();
  assert(parameter.Set(3.0F, ParameterWritePhase::kStartup, config) ==
         Status::kInvalidState);
  assert(parameter.Set(3.0F, ParameterWritePhase::kRuntime, config) ==
         Status::kInvalidState);
  assert(parameter.value() == 2.0F);
}

void RuntimeParametersAreValidatedAndObservable() {
  constexpr ParameterDescriptor<float> descriptor{
      "controller.kd", "N/A", 1.0F, 0.0F, 10.0F,
      ParameterMutability::kRuntime, ParameterPersistence::kVolatile};
  UpdateState update;
  Parameter parameter(descriptor, ValidateGain, &update);
  const ExecutionContext config("config", ExecutionKind::kThread, 1);
  const ExecutionContext interrupt("uart-rx", ExecutionKind::kInterrupt, 8);

  parameter.SealStartup();
  assert(parameter.Set(11.0F, ParameterWritePhase::kRuntime, config) ==
         Status::kInvalidArgument);
  assert(parameter.Set(4.0F, ParameterWritePhase::kRuntime, config) ==
         Status::kUnavailable);
  assert(parameter.value() == 1.0F);
  assert(parameter.Set(3.0F, ParameterWritePhase::kRuntime, interrupt) ==
         Status::kInvalidArgument);
  assert(parameter.Set(3.0F, ParameterWritePhase::kRuntime, config) ==
         Status::kOk);
  assert(parameter.value() == 3.0F);
  assert(parameter.revision() == 1);
  assert(update.calls == 2);
  assert(update.last_candidate == 3.0F);
  assert(parameter.stats().rejected == 3);
  assert(parameter.stats().updates == 1);
}

}  // namespace

int main() {
  BuildTimeParametersCannotBeMutated();
  StartupParametersLockWhenConfigurationIsSealed();
  RuntimeParametersAreValidatedAndObservable();
  return 0;
}
