#include "aster_runtime/execution.hpp"

namespace aster {
namespace {
thread_local const ExecutionScope* active_scope{};
}

ExecutionScope::ExecutionScope(std::string_view executor, const aster_clock_base_t* clock) noexcept
    : previous_(active_scope), executor_(executor), clock_(clock) {
  active_scope = this;
}
ExecutionScope::~ExecutionScope() { active_scope = previous_; }

Status ResolveCallContext(const aster_execution_context_t* requested, ExecutionContext& result,
                          bool allow_interrupt) noexcept {
  const auto native = NativeExecutionContext();
  if (native.kind() == ExecutionKind::kInterrupt) {
    if (!allow_interrupt) return Status::kInvalidState;
    result = native;
    return Status::kOk;
  }
  if (requested != nullptr) {
    const auto status = ValidateAbiExecutionContext(requested);
    if (!IsOk(status)) return status;
    result = FromAbiExecutionContext(*requested);
    return !allow_interrupt && result.kind() == ExecutionKind::kInterrupt ? Status::kInvalidState
                                                                          : Status::kOk;
  }
  if (active_scope == nullptr) {
    result = native;
    return Status::kOk;
  }
  auto now = native.timestamp_ns();
  const auto* clock = active_scope->clock_;
  if (clock != nullptr) {
    if (clock->struct_size < sizeof(*clock) || clock->now_ns == nullptr)
      return Status::kUnavailable;
    const auto status = FromAbiStatus(clock->now_ns(clock->impl, &now));
    if (!IsOk(status)) return status;
  }
  result = ExecutionContext(active_scope->executor_, ExecutionKind::kThread, now);
  return Status::kOk;
}
}  // namespace aster
