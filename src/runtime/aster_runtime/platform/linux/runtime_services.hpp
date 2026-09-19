#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <new>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "aster_module_cpp_interface/core_ref.hpp"
#include "aster_runtime/core/allocator.hpp"
#include "aster_runtime/core/clock.hpp"
#include "aster_runtime/core/executor.hpp"
#include "aster_runtime/core/logger.hpp"
#include "aster_runtime/execution.hpp"

namespace aster::platform::linux {

class SteadyClock final : public Clock {
 public:
  [[nodiscard]] ClockDomain domain() const noexcept override { return ClockDomain::kMonotonic; }

  [[nodiscard]] std::uint64_t NowNs() const noexcept override {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
  }
};

class StderrLogger final : public Logger {
 public:
  explicit StderrLogger(std::string_view instance = {},
                        LogLevel threshold = LogLevel::kTrace) noexcept
      : instance_(instance), threshold_(threshold) {}
  Status Write(LogLevel level, std::string_view message,
               const ExecutionContext& caller) noexcept override {
    if (level < threshold_) return Status::kOk;
    static std::mutex output_mutex;
    const std::lock_guard lock(output_mutex);
    const auto prefix = Prefix(level);
    if (std::fwrite(prefix.data(), 1, prefix.size(), stderr) != prefix.size() ||
        std::fwrite(" [", 1, 2, stderr) != 2 ||
        (!instance_.empty() &&
         (std::fwrite(instance_.data(), 1, instance_.size(), stderr) != instance_.size() ||
          std::fwrite("/", 1, 1, stderr) != 1)) ||
        std::fwrite(caller.executor_name().data(), 1, caller.executor_name().size(), stderr) !=
            caller.executor_name().size() ||
        std::fwrite("] ", 1, 2, stderr) != 2 ||
        std::fwrite(message.data(), 1, message.size(), stderr) != message.size() ||
        std::fwrite("\n", 1, 1, stderr) != 1) {
      return Status::kInternal;
    }
    return Status::kOk;
  }

 private:
  [[nodiscard]] static constexpr std::string_view Prefix(LogLevel level) noexcept {
    switch (level) {
      case LogLevel::kTrace:
        return "TRACE";
      case LogLevel::kDebug:
        return "DEBUG";
      case LogLevel::kInfo:
        return "INFO";
      case LogLevel::kWarning:
        return "WARN";
      case LogLevel::kError:
        return "ERROR";
      case LogLevel::kCritical:
        return "CRITICAL";
    }
    return "UNKNOWN";
  }

  std::string_view instance_;
  LogLevel threshold_;
};

class SystemAllocator final : public Allocator {
 public:
  void* Allocate(std::size_t size, std::size_t alignment) noexcept override {
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1U)) != 0) {
      return nullptr;
    }
    return ::operator new(size, std::align_val_t(alignment), std::nothrow);
  }

  void Deallocate(void* memory, std::size_t, std::size_t alignment) noexcept override {
    if (memory != nullptr) {
      ::operator delete(memory, std::align_val_t(alignment));
    }
  }
};

enum class ThreadExecutorState : std::uint8_t {
  kConstructed,
  kPrepared,
  kRunning,
  kQuiescing,
  kStopped,
  kFailed,
};

template <std::size_t Capacity>
class ThreadExecutor final : public Executor {
 public:
  ThreadExecutor(std::string_view name, Clock& clock) noexcept
    requires(Capacity > 0)
      : name_(name), clock_(clock) {}
  ThreadExecutor(std::string_view name, Clock& clock, std::size_t queue_depth, std::size_t threads)
    requires(Capacity == 0)
      : name_(name), clock_(clock), queue_(queue_depth), thread_count_(threads) {}
  ~ThreadExecutor() override { Shutdown(); }

  ThreadExecutor(const ThreadExecutor&) = delete;
  ThreadExecutor& operator=(const ThreadExecutor&) = delete;

  Status Prepare() noexcept {
    std::unique_lock lock(mutex_);
    if (state_ != ThreadExecutorState::kConstructed || name_.empty()) {
      return Status::kInvalidState;
    }
    if (queue_.empty() || thread_count_ == 0) return Status::kInvalidArgument;
    state_ = ThreadExecutorState::kPrepared;
    try {
      workers_.reserve(thread_count_);
      for (std::size_t i = 0; i < thread_count_; ++i) workers_.emplace_back([this] { Run(); });
    } catch (...) {
      state_ = ThreadExecutorState::kFailed;
      lock.unlock();
      ready_.notify_all();
      for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
      return Status::kUnavailable;
    }
    return Status::kOk;
  }

  Status Activate() noexcept {
    {
      const std::lock_guard lock(mutex_);
      if (state_ != ThreadExecutorState::kPrepared) {
        return Status::kInvalidState;
      }
      state_ = ThreadExecutorState::kRunning;
    }
    ready_.notify_all();
    return Status::kOk;
  }

  Status Start() noexcept {
    auto status = Prepare();
    if (IsOk(status)) {
      status = Activate();
      if (!IsOk(status)) {
        Shutdown();
      }
    }
    return status;
  }

  void Shutdown() noexcept {
    for (const auto& worker : workers_)
      if (worker.joinable() && worker.get_id() == std::this_thread::get_id()) std::terminate();
    {
      const std::lock_guard lock(mutex_);
      if (state_ == ThreadExecutorState::kStopped || state_ == ThreadExecutorState::kQuiescing ||
          state_ == ThreadExecutorState::kFailed) {
        return;
      }
      if (state_ == ThreadExecutorState::kConstructed) {
        state_ = ThreadExecutorState::kStopped;
        return;
      }
      state_ = ThreadExecutorState::kQuiescing;
      size_ = 0;
    }
    ready_.notify_all();
    for (auto& worker : workers_)
      if (worker.joinable()) worker.join();
    const std::lock_guard lock(mutex_);
    state_ = ThreadExecutorState::kStopped;
  }

  [[nodiscard]] std::string_view Name() const noexcept override { return name_; }

  Status TryPost(WorkItem work, const ExecutionContext& caller) noexcept override {
    return TryPostAt(clock_.NowNs(), work, caller);
  }

  Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                   const ExecutionContext& caller) noexcept override {
    if (!work || caller.kind() == ExecutionKind::kInterrupt) {
      return Status::kInvalidArgument;
    }
    std::uint64_t ticket{};
    return Enqueue(work, timestamp_ns, true, ticket);
  }

  // Runtime-only admission control for asynchronous completions. A reservation
  // occupies one queue slot until armed, cancelled, or discarded by Shutdown.
  Status Reserve(WorkItem work, std::uint64_t& ticket) noexcept {
    return Enqueue(work, 0, false, ticket);
  }
  Status ScheduleReserved(std::uint64_t ticket, std::uint64_t timestamp_ns) noexcept {
    const std::lock_guard lock(mutex_);
    if (state_ != ThreadExecutorState::kPrepared && state_ != ThreadExecutorState::kRunning)
      return Status::kInvalidState;
    for (auto& entry : queue_) {
      if (entry.work && entry.sequence == ticket) {
        entry.timestamp_ns = timestamp_ns;
        entry.armed = true;
        ready_.notify_all();
        return Status::kOk;
      }
    }
    return Status::kNotFound;
  }
  Status CancelReserved(std::uint64_t ticket) noexcept {
    const std::lock_guard lock(mutex_);
    if (state_ != ThreadExecutorState::kPrepared && state_ != ThreadExecutorState::kRunning)
      return Status::kInvalidState;
    for (auto& entry : queue_) {
      if (entry.work && entry.sequence == ticket) {
        entry = {};
        --size_;
        ready_.notify_all();
        return Status::kOk;
      }
    }
    return Status::kNotFound;
  }

  [[nodiscard]] ThreadExecutorState state() const noexcept {
    const std::lock_guard lock(mutex_);
    return state_;
  }

 private:
  Status Enqueue(WorkItem work, std::uint64_t timestamp_ns, bool armed,
                 std::uint64_t& ticket) noexcept {
    if (!work) return Status::kInvalidArgument;
    {
      const std::lock_guard lock(mutex_);
      if (state_ != ThreadExecutorState::kPrepared && state_ != ThreadExecutorState::kRunning) {
        return Status::kInvalidState;
      }
      if (size_ == queue_.size()) {
        return Status::kCapacityExceeded;
      }
      for (auto& entry : queue_) {
        if (entry.work) continue;
        ticket = next_sequence_++;
        entry = {timestamp_ns, ticket, work, armed};
        ++size_;
        break;
      }
    }
    ready_.notify_one();
    return Status::kOk;
  }

  struct Entry {
    std::uint64_t timestamp_ns{};
    std::uint64_t sequence{};
    WorkItem work{};
    bool armed{};
  };

  [[nodiscard]] std::size_t NextIndex() const noexcept {
    auto result = queue_.size();
    for (std::size_t index = 0; index < queue_.size(); ++index) {
      if (!queue_[index].work || !queue_[index].armed) continue;
      if (result == queue_.size() || queue_[index].timestamp_ns < queue_[result].timestamp_ns ||
          (queue_[index].timestamp_ns == queue_[result].timestamp_ns &&
           queue_[index].sequence < queue_[result].sequence)) {
        result = index;
      }
    }
    return result;
  }

  void Run() noexcept {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [this] { return state_ != ThreadExecutorState::kPrepared; });
    while (state_ == ThreadExecutorState::kRunning) {
      const auto index = NextIndex();
      if (index == queue_.size()) {
        ready_.wait(lock);
        continue;
      }
      const auto now_ns = clock_.NowNs();
      if (queue_[index].timestamp_ns > now_ns) {
        const auto delay = queue_[index].timestamp_ns - now_ns;
        const auto maximum = static_cast<std::uint64_t>(std::chrono::nanoseconds::max().count());
        ready_.wait_for(
            lock, std::chrono::nanoseconds(static_cast<std::int64_t>(std::min(delay, maximum))));
        continue;
      }
      const auto work = queue_[index].work;
      queue_[index] = {};
      --size_;
      lock.unlock();
      const ExecutionScope scope(name_, clock_.NativeHandle());
      work.Run(ExecutionContext(name_, ExecutionKind::kThread, clock_.NowNs()));
      lock.lock();
    }
  }

  std::string_view name_;
  Clock& clock_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::vector<std::thread> workers_;
  std::conditional_t<Capacity == 0, std::vector<Entry>, std::array<Entry, Capacity>> queue_{};
  std::size_t thread_count_{1};
  std::size_t size_{};
  std::uint64_t next_sequence_{};
  ThreadExecutorState state_{ThreadExecutorState::kConstructed};
};

}  // namespace aster::platform::linux
