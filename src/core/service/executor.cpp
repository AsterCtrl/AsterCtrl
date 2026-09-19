#include "aster_runtime/core/executor.hpp"

#include "aster_runtime/execution.hpp"
#include "validation.hpp"

namespace aster {

Executor::Executor() noexcept
    : native_{sizeof(aster_executor_base_t), this, ExecutorGetName, ExecutorTryPost,
              ExecutorTryPostAt} {}

aster_status_t Executor::ExecutorGetName(void* context, aster_string_view_t* name) noexcept {
  if (context == nullptr || name == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  *name = {};
  auto& self = *static_cast<Executor*>(context);
  const auto value = self.Name();
  if (value.empty()) {
    return ASTER_STATUS_INTERNAL;
  }
  *name = ToAbiString(value);
  return ASTER_STATUS_OK;
}

aster_status_t Executor::ExecutorTryPost(void* context, aster_work_fn_t callback,
                                         void* callback_state,
                                         const aster_execution_context_t* caller) noexcept {
  if (context == nullptr || callback == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ExecutionContext resolved("unmanaged", ExecutionKind::kThread, 0);
  const auto context_status = ResolveCallContext(caller, resolved, true);
  if (!IsOk(context_status)) {
    return ToAbiStatus(context_status);
  }
  auto& self = *static_cast<Executor*>(context);
  return ToAbiStatus(self.TryPost({callback, callback_state}, resolved));
}

aster_status_t Executor::ExecutorTryPostAt(void* context, std::uint64_t timestamp_ns,
                                           aster_work_fn_t callback, void* callback_state,
                                           const aster_execution_context_t* caller) noexcept {
  if (context == nullptr || callback == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ExecutionContext resolved("unmanaged", ExecutionKind::kThread, 0);
  const auto context_status = ResolveCallContext(caller, resolved, true);
  if (!IsOk(context_status)) {
    return ToAbiStatus(context_status);
  }
  auto& self = *static_cast<Executor*>(context);
  return ToAbiStatus(self.TryPostAt(timestamp_ns, {callback, callback_state}, resolved));
}

}  // namespace aster
