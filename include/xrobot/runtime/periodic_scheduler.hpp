#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "xrobot/runtime/executor.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::runtime {

enum class PeriodicSchedulerState : std::uint8_t {
  kConstructed,
  kInitialized,
  kRunning,
  kStopped,
  kFailed,
};

struct PeriodicTaskStats {
  std::uint32_t releases{};
  std::uint32_t completed{};
  std::uint32_t skipped{};
  std::uint32_t schedule_failures{};
  bool pending{};
};

struct PeriodicTaskDescriptor {
  std::string_view module_name;
  std::string_view task_name;
  std::uint64_t period_ns{};
  Executor* executor{};
};

class PeriodicTaskBinder {
 public:
  virtual ~PeriodicTaskBinder() = default;

  virtual Status BindPeriodicTask(std::string_view module_name,
                                  std::string_view task_name,
                                  WorkItem work) noexcept = 0;
};

class PeriodicScheduler : public PeriodicTaskBinder {
 public:
  virtual ~PeriodicScheduler() = default;

  virtual std::string_view Name() const noexcept = 0;
  virtual PeriodicSchedulerState state() const noexcept = 0;
  virtual std::size_t task_count() const noexcept = 0;
  virtual PeriodicTaskDescriptor task_descriptor(
      std::size_t index) const noexcept = 0;
  virtual PeriodicTaskStats task_stats(std::size_t index) const noexcept = 0;

  virtual Status Initialize() noexcept = 0;
  virtual Status Start() noexcept = 0;
  virtual Status Poll(std::uint64_t now_ns,
                      const ExecutionContext& caller) noexcept = 0;
  virtual void Shutdown() noexcept = 0;
};

template <std::size_t Capacity>
class StaticPeriodicScheduler final : public PeriodicScheduler {
 public:
  static_assert(Capacity > 0);
  static_assert(std::atomic_bool::is_always_lock_free);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  explicit constexpr StaticPeriodicScheduler(std::string_view name) noexcept
      : name_(name) {}

  Status AddTask(std::string_view module_name, std::string_view task_name,
                 std::uint64_t period_ns, Executor& executor) noexcept {
    if (state() != PeriodicSchedulerState::kConstructed) {
      return Status::kInvalidState;
    }
    if (module_name.empty() || task_name.empty() || period_ns == 0) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (slots_[index].module_name == module_name &&
          slots_[index].task_name == task_name) {
        return Status::kInvalidArgument;
      }
    }
    if (size_ == Capacity) {
      return Status::kCapacityExceeded;
    }

    auto& slot = slots_[size_++];
    slot.owner = this;
    slot.module_name = module_name;
    slot.task_name = task_name;
    slot.period_ns = period_ns;
    slot.executor = &executor;
    return Status::kOk;
  }

  Status BindPeriodicTask(std::string_view module_name,
                          std::string_view task_name,
                          WorkItem work) noexcept override {
    if (state() != PeriodicSchedulerState::kConstructed) {
      return Status::kInvalidState;
    }
    if (module_name.empty() || task_name.empty() || !work) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      auto& slot = slots_[index];
      if (slot.module_name == module_name && slot.task_name == task_name) {
        if (slot.work) {
          return Status::kInvalidState;
        }
        slot.work = work;
        return Status::kOk;
      }
    }
    return Status::kUnavailable;
  }

  std::string_view Name() const noexcept override { return name_; }

  PeriodicSchedulerState state() const noexcept override {
    return state_.load(std::memory_order_acquire);
  }

  std::size_t task_count() const noexcept override { return size_; }

  PeriodicTaskDescriptor task_descriptor(
      std::size_t index) const noexcept override {
    if (index >= size_) {
      return {};
    }
    const auto& slot = slots_[index];
    return {slot.module_name, slot.task_name, slot.period_ns, slot.executor};
  }

  PeriodicTaskStats task_stats(std::size_t index) const noexcept override {
    if (index >= size_) {
      return {};
    }
    const auto& slot = slots_[index];
    return {
        .releases = slot.releases.load(std::memory_order_relaxed),
        .completed = slot.completed.load(std::memory_order_relaxed),
        .skipped = slot.skipped.load(std::memory_order_relaxed),
        .schedule_failures =
            slot.schedule_failures.load(std::memory_order_relaxed),
        .pending = slot.pending.load(std::memory_order_acquire),
    };
  }

  Status Initialize() noexcept override {
    if (state() != PeriodicSchedulerState::kConstructed || name_.empty() ||
        size_ == 0) {
      state_.store(PeriodicSchedulerState::kFailed,
                   std::memory_order_release);
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      const auto& slot = slots_[index];
      if (slot.executor == nullptr || !slot.work) {
        state_.store(PeriodicSchedulerState::kFailed,
                     std::memory_order_release);
        return Status::kInvalidState;
      }
    }
    state_.store(PeriodicSchedulerState::kInitialized,
                 std::memory_order_release);
    return Status::kOk;
  }

  Status Start() noexcept override {
    if (state() != PeriodicSchedulerState::kInitialized) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      auto& slot = slots_[index];
      slot.pending.store(false, std::memory_order_relaxed);
      slot.armed = false;
      slot.next_release_ns = 0;
    }
    has_last_poll_ = false;
    last_poll_ns_ = 0;
    state_.store(PeriodicSchedulerState::kRunning,
                 std::memory_order_release);
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns,
              const ExecutionContext& caller) noexcept override {
    if (state() != PeriodicSchedulerState::kRunning) {
      return Status::kInvalidState;
    }
    if (caller.kind() == ExecutionKind::kInterrupt ||
        (has_last_poll_ && now_ns < last_poll_ns_)) {
      return Status::kInvalidArgument;
    }
    has_last_poll_ = true;
    last_poll_ns_ = now_ns;

    for (std::size_t index = 0; index < size_; ++index) {
      auto& slot = slots_[index];
      if (!slot.armed) {
        slot.armed = true;
        slot.next_release_ns = now_ns;
      }
      if (now_ns < slot.next_release_ns) {
        continue;
      }

      const std::uint64_t elapsed_periods =
          (now_ns - slot.next_release_ns) / slot.period_ns;
      const std::uint64_t due_releases =
          elapsed_periods == std::numeric_limits<std::uint64_t>::max()
              ? elapsed_periods
              : elapsed_periods + 1;
      Advance(slot, due_releases);

      if (slot.pending.exchange(true, std::memory_order_acq_rel)) {
        AddSkipped(slot, due_releases);
        continue;
      }
      if (due_releases > 1) {
        AddSkipped(slot, due_releases - 1);
      }

      const auto status =
          slot.executor->TryPost({Dispatch, &slot}, caller);
      if (!IsOk(status)) {
        slot.pending.store(false, std::memory_order_release);
        slot.schedule_failures.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      slot.releases.fetch_add(1, std::memory_order_relaxed);
    }
    return Status::kOk;
  }

  void Shutdown() noexcept override {
    state_.store(PeriodicSchedulerState::kStopped,
                 std::memory_order_release);
  }

 private:
  struct Slot {
    StaticPeriodicScheduler* owner{};
    std::string_view module_name;
    std::string_view task_name;
    std::uint64_t period_ns{};
    Executor* executor{};
    WorkItem work{};
    std::atomic_bool pending{};
    std::atomic<std::uint32_t> releases{};
    std::atomic<std::uint32_t> completed{};
    std::atomic<std::uint32_t> skipped{};
    std::atomic<std::uint32_t> schedule_failures{};
    std::uint64_t next_release_ns{};
    bool armed{};
  };

  static void Dispatch(void* state,
                       const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<Slot*>(state);
    if (slot.owner->state() == PeriodicSchedulerState::kRunning) {
      slot.work.Run(context);
      slot.completed.fetch_add(1, std::memory_order_relaxed);
    } else {
      slot.skipped.fetch_add(1, std::memory_order_relaxed);
    }
    slot.pending.store(false, std::memory_order_release);
  }

  static void AddSkipped(Slot& slot, std::uint64_t count) noexcept {
    const auto bounded = static_cast<std::uint32_t>(
        count > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : count);
    slot.skipped.fetch_add(bounded, std::memory_order_relaxed);
  }

  static void Advance(Slot& slot, std::uint64_t periods) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (periods > (maximum - slot.next_release_ns) / slot.period_ns) {
      slot.next_release_ns = maximum;
      return;
    }
    slot.next_release_ns += periods * slot.period_ns;
  }

  std::string_view name_;
  std::array<Slot, Capacity> slots_{};
  std::size_t size_{};
  std::atomic<PeriodicSchedulerState> state_{
      PeriodicSchedulerState::kConstructed};
  std::uint64_t last_poll_ns_{};
  bool has_last_poll_{};
};

}  // namespace xrobot::runtime
