#pragma once

#include <cstdint>
#include <string_view>

#include "aster/execution.hpp"
#include "aster/status.hpp"

namespace aster {

class Executor {
 public:
  virtual ~Executor() = default;
  [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
  virtual Status TryPost(WorkItem work, const ExecutionContext& caller) noexcept = 0;
  virtual Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                           const ExecutionContext& caller) noexcept = 0;
};

class ExecutorRef {
 public:
  constexpr ExecutorRef() noexcept = default;
  constexpr explicit ExecutorRef(Executor& executor) noexcept : executor_(&executor) {}

  [[nodiscard]] std::string_view Name() const noexcept {
    return executor_ == nullptr ? std::string_view{} : executor_->Name();
  }
  Status TryPost(WorkItem work, const ExecutionContext& caller) const noexcept {
    return executor_ == nullptr ? Status::kUnavailable : executor_->TryPost(work, caller);
  }
  Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                   const ExecutionContext& caller) const noexcept {
    return executor_ == nullptr ? Status::kUnavailable
                                : executor_->TryPostAt(timestamp_ns, work, caller);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return executor_ != nullptr; }

 private:
  Executor* executor_{};
};

}  // namespace aster
