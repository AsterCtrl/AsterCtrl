#pragma once

#include "aster_module_cpp_interface/executor.hpp"

namespace aster {

class Executor {
 public:
  Executor() noexcept;
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  [[nodiscard]] const aster_executor_base_t* NativeHandle() const noexcept { return &native_; }
  virtual ~Executor() = default;
  [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
  virtual Status TryPost(WorkItem work, const ExecutionContext& caller) noexcept = 0;
  virtual Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                           const ExecutionContext& caller) noexcept = 0;

 private:
  static aster_status_t ExecutorGetName(void* context, aster_string_view_t* name) noexcept;
  static aster_status_t ExecutorTryPost(void* context, aster_work_fn_t callback,
                                        void* callback_state,
                                        const aster_execution_context_t* caller) noexcept;
  static aster_status_t ExecutorTryPostAt(void* context, std::uint64_t timestamp_ns,
                                          aster_work_fn_t callback, void* callback_state,
                                          const aster_execution_context_t* caller) noexcept;
  aster_executor_base_t native_;
};

}  // namespace aster
