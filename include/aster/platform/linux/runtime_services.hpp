#pragma once

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

#include "aster/core_ref.hpp"

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
  Status Write(LogLevel level, std::string_view message,
               const ExecutionContext& caller) noexcept override {
    const std::lock_guard lock(mutex_);
    const auto prefix = Prefix(level);
    if (std::fwrite(prefix.data(), 1, prefix.size(), stderr) != prefix.size() ||
        std::fwrite(" [", 1, 2, stderr) != 2 ||
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

  std::mutex mutex_;
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
  static_assert(Capacity > 0);

  ThreadExecutor(std::string_view name, Clock& clock) noexcept : name_(name), clock_(clock) {}
  ~ThreadExecutor() override { Shutdown(); }

  ThreadExecutor(const ThreadExecutor&) = delete;
  ThreadExecutor& operator=(const ThreadExecutor&) = delete;

  Status Prepare() noexcept {
    const std::lock_guard lock(mutex_);
    if (state_ != ThreadExecutorState::kConstructed || name_.empty()) {
      return Status::kInvalidState;
    }
    state_ = ThreadExecutorState::kPrepared;
    try {
      worker_ = std::thread([this] { Run(); });
    } catch (...) {
      state_ = ThreadExecutorState::kFailed;
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
    if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()) {
      std::terminate();
    }
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
    if (worker_.joinable()) {
      worker_.join();
    }
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
    {
      const std::lock_guard lock(mutex_);
      if (state_ != ThreadExecutorState::kPrepared && state_ != ThreadExecutorState::kRunning) {
        return Status::kInvalidState;
      }
      if (size_ == queue_.size()) {
        return Status::kCapacityExceeded;
      }
      queue_[size_++] = {timestamp_ns, next_sequence_++, work};
    }
    ready_.notify_one();
    return Status::kOk;
  }

  [[nodiscard]] ThreadExecutorState state() const noexcept {
    const std::lock_guard lock(mutex_);
    return state_;
  }

 private:
  struct Entry {
    std::uint64_t timestamp_ns{};
    std::uint64_t sequence{};
    WorkItem work{};
  };

  [[nodiscard]] std::size_t NextIndex() const noexcept {
    std::size_t result{};
    for (std::size_t index = 1; index < size_; ++index) {
      if (queue_[index].timestamp_ns < queue_[result].timestamp_ns ||
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
      if (size_ == 0) {
        ready_.wait(lock, [this] { return state_ != ThreadExecutorState::kRunning || size_ != 0; });
        continue;
      }
      const auto index = NextIndex();
      const auto now_ns = clock_.NowNs();
      if (queue_[index].timestamp_ns > now_ns) {
        const auto delay = queue_[index].timestamp_ns - now_ns;
        ready_.wait_for(lock, std::chrono::nanoseconds(delay));
        continue;
      }
      const auto work = queue_[index].work;
      for (std::size_t move = index + 1; move < size_; ++move) {
        queue_[move - 1] = queue_[move];
      }
      --size_;
      lock.unlock();
      work.Run(ExecutionContext(name_, ExecutionKind::kThread, clock_.NowNs()));
      lock.lock();
    }
  }

  std::string_view name_;
  Clock& clock_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::thread worker_;
  std::array<Entry, Capacity> queue_{};
  std::size_t size_{};
  std::uint64_t next_sequence_{};
  ThreadExecutorState state_{ThreadExecutorState::kConstructed};
};

}  // namespace aster::platform::linux
