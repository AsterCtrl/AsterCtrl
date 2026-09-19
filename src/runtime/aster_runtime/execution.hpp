#pragma once

#include "aster_module_c_interface/clock/clock_base.h"
#include "aster_module_cpp_interface/execution.hpp"

namespace aster {

// Platform implementations identify actual ISR execution and supply monotonic
// time for calls from threads not created by AsterCtrl.
ExecutionContext NativeExecutionContext() noexcept;
Status ResolveCallContext(const aster_execution_context_t* requested, ExecutionContext& result,
                          bool allow_interrupt = false) noexcept;

// Runtime-owned scope, never linked into a Module Package. Nested callbacks
// restore the preceding executor/clock when they return.
class ExecutionScope final {
 public:
  ExecutionScope(std::string_view executor, const aster_clock_base_t* clock = nullptr) noexcept;
  ~ExecutionScope();
  ExecutionScope(const ExecutionScope&) = delete;
  ExecutionScope& operator=(const ExecutionScope&) = delete;

 private:
  friend Status ResolveCallContext(const aster_execution_context_t*, ExecutionContext&,
                                   bool) noexcept;
  const ExecutionScope* previous_;
  std::string_view executor_;
  const aster_clock_base_t* clock_;
};
}  // namespace aster
