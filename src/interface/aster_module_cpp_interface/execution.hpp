#pragma once

#include <cstdint>
#include <string_view>

#include "aster_module_c_interface/executor/execution_context.h"
#include "aster_module_c_interface/executor/executor_base.h"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster {

enum class ExecutionKind : std::uint8_t {
  kThread,
  kInterrupt,
};

class ExecutionContext {
 public:
  constexpr ExecutionContext(std::string_view executor_name, ExecutionKind kind,
                             std::uint64_t timestamp_ns) noexcept
      : executor_name_(executor_name), kind_(kind), timestamp_ns_(timestamp_ns) {}

  [[nodiscard]] constexpr std::string_view executor_name() const noexcept { return executor_name_; }
  [[nodiscard]] constexpr ExecutionKind kind() const noexcept { return kind_; }
  [[nodiscard]] constexpr std::uint64_t timestamp_ns() const noexcept { return timestamp_ns_; }

 private:
  std::string_view executor_name_;
  ExecutionKind kind_;
  std::uint64_t timestamp_ns_{};
};

[[nodiscard]] constexpr aster_execution_context_t ToAbiExecutionContext(
    const ExecutionContext& context) noexcept {
  return {
      {context.executor_name().data(), context.executor_name().size()},
      context.kind() == ExecutionKind::kThread ? ASTER_EXECUTION_KIND_THREAD
                                               : ASTER_EXECUTION_KIND_INTERRUPT,
      context.timestamp_ns(),
  };
}

[[nodiscard]] constexpr ExecutionContext FromAbiExecutionContext(
    const aster_execution_context_t& context) noexcept {
  return {
      context.executor_name.data == nullptr
          ? std::string_view{}
          : std::string_view(context.executor_name.data, context.executor_name.size),
      context.kind == ASTER_EXECUTION_KIND_THREAD ? ExecutionKind::kThread
                                                  : ExecutionKind::kInterrupt,
      context.timestamp_ns,
  };
}

[[nodiscard]] constexpr Status ValidateAbiExecutionContext(
    const aster_execution_context_t* context) noexcept {
  if (context == nullptr) {
    return Status::kInvalidArgument;
  }
  return context->executor_name.data != nullptr && context->executor_name.size != 0 &&
                 context->kind <= ASTER_EXECUTION_KIND_INTERRUPT
             ? Status::kOk
             : Status::kInvalidArgument;
}

using WorkCallback = void (*)(void*, const ExecutionContext&) noexcept;

struct WorkItem {
  aster_work_fn_t callback{};
  void* state{};

  template <WorkCallback Callback>
  [[nodiscard]] static constexpr WorkItem Bind(void* state) noexcept {
    return {Invoke<Callback>, state};
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return callback != nullptr; }

  void Run(const ExecutionContext& context) const noexcept {
    if (callback != nullptr) {
      const auto abi_context = ToAbiExecutionContext(context);
      callback(state, &abi_context);
    }
  }

 private:
  template <WorkCallback Callback>
  static void Invoke(void* state, const aster_execution_context_t* context) noexcept {
    if (IsOk(ValidateAbiExecutionContext(context))) {
      Callback(state, FromAbiExecutionContext(*context));
    }
  }
};

}  // namespace aster
