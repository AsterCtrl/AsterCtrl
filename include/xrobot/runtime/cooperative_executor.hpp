#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "xrobot/runtime/executor.hpp"

namespace xrobot::runtime {

template <std::size_t Capacity>
class CooperativeExecutor final : public Executor {
 public:
  static_assert(Capacity > 0);

  constexpr CooperativeExecutor(std::string_view name,
                                std::uint8_t priority) noexcept
      : name_(name), context_(name, ExecutionKind::kThread, priority) {}

  std::string_view Name() const noexcept override { return name_; }
  const ExecutionContext& context() const noexcept override { return context_; }
  ExecutorState state() const noexcept override { return state_; }
  ExecutorStats stats() const noexcept override { return stats_; }
  std::size_t pending() const noexcept { return size_; }

  Status Initialize() noexcept override {
    if (state_ != ExecutorState::kConstructed || name_.empty()) {
      return Status::kInvalidState;
    }
    head_ = 0;
    tail_ = 0;
    size_ = 0;
    stats_ = {};
    state_ = ExecutorState::kInitialized;
    return Status::kOk;
  }

  Status Start() noexcept override {
    if (state_ != ExecutorState::kInitialized) {
      return Status::kInvalidState;
    }
    state_ = ExecutorState::kRunning;
    return Status::kOk;
  }

  Status TryPost(WorkItem work,
                 const ExecutionContext& caller) noexcept override {
    if (state_ != ExecutorState::kRunning) {
      return Status::kInvalidState;
    }
    if (!work || caller.kind() == ExecutionKind::kInterrupt) {
      ++stats_.rejected;
      return Status::kInvalidArgument;
    }
    if (size_ == Capacity) {
      ++stats_.rejected;
      return Status::kCapacityExceeded;
    }

    queue_[tail_] = work;
    tail_ = (tail_ + 1) % Capacity;
    ++size_;
    ++stats_.accepted;
    if (size_ > stats_.high_watermark) {
      stats_.high_watermark = size_;
    }
    return Status::kOk;
  }

  Status RunOne() noexcept {
    if (state_ != ExecutorState::kRunning) {
      return Status::kInvalidState;
    }
    if (size_ == 0) {
      return Status::kUnavailable;
    }

    const auto work = queue_[head_];
    head_ = (head_ + 1) % Capacity;
    --size_;
    ++stats_.executed;
    work.Run(context_);
    return Status::kOk;
  }

  void Shutdown() noexcept override {
    size_ = 0;
    head_ = 0;
    tail_ = 0;
    state_ = ExecutorState::kStopped;
  }

 private:
  std::string_view name_;
  ExecutionContext context_;
  std::array<WorkItem, Capacity> queue_{};
  std::size_t head_{};
  std::size_t tail_{};
  std::size_t size_{};
  ExecutorStats stats_{};
  ExecutorState state_{ExecutorState::kConstructed};
};

}  // namespace xrobot::runtime
