#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/core_ref.hpp"
#include "aster/static_hardware.hpp"

namespace aster::platform::zephyr {

class UptimeClock final : public Clock {
 public:
  [[nodiscard]] ClockDomain domain() const noexcept override { return ClockDomain::kMonotonic; }

  [[nodiscard]] std::uint64_t NowNs() const noexcept override {
    return k_ticks_to_ns_floor64(k_uptime_ticks());
  }
};

class PrintkLogger final : public Logger {
 public:
  Status Write(LogLevel level, std::string_view message,
               const ExecutionContext& caller) noexcept override {
    if (message.size() > static_cast<std::size_t>(INT_MAX) ||
        caller.executor_name().size() > static_cast<std::size_t>(INT_MAX)) {
      return Status::kCapacityExceeded;
    }
    printk("%s [%.*s] %.*s\n", Prefix(level), static_cast<int>(caller.executor_name().size()),
           caller.executor_name().data(), static_cast<int>(message.size()), message.data());
    return Status::kOk;
  }

 private:
  [[nodiscard]] static constexpr const char* Prefix(LogLevel level) noexcept {
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
};

template <std::size_t Capacity>
class FixedArenaAllocator final : public Allocator {
 public:
  static_assert(Capacity > 0);

  void* Allocate(std::size_t size, std::size_t alignment) noexcept override {
    if (size == 0 || alignment == 0 || alignment > alignof(std::max_align_t) ||
        (alignment & (alignment - 1U)) != 0) {
      return nullptr;
    }
    const auto aligned = (offset_ + alignment - 1U) & ~(alignment - 1U);
    if (aligned > storage_.size() || size > storage_.size() - aligned) {
      return nullptr;
    }
    auto* result = storage_.data() + aligned;
    offset_ = aligned + size;
    return result;
  }

  void Deallocate(void*, std::size_t, std::size_t) noexcept override {}

  void Reset() noexcept { offset_ = 0; }
  [[nodiscard]] std::size_t used() const noexcept { return offset_; }

 private:
  alignas(std::max_align_t) std::array<std::byte, Capacity> storage_{};
  std::size_t offset_{};
};

template <std::size_t MaxCapabilities>
using StaticHardwareManager = ::aster::StaticHardwareManager<MaxCapabilities>;

template <std::size_t Capacity>
class ThreadExecutor final : public Executor {
 public:
  static_assert(Capacity > 0);

  ThreadExecutor(std::string_view name, Clock& clock, k_thread_stack_t* stack,
                 std::size_t stack_size, int priority) noexcept
      : name_(name), clock_(clock), stack_(stack), stack_size_(stack_size), priority_(priority) {
    k_msgq_init(&queue_, queue_storage_.data(), sizeof(Entry), Capacity);
  }

  Status Start() noexcept {
    if (running_ || name_.empty() || stack_ == nullptr || stack_size_ == 0) {
      return Status::kInvalidState;
    }
    running_ = true;
    thread_id_ = k_thread_create(&thread_, stack_, stack_size_, EntryPoint, this, nullptr, nullptr,
                                 priority_, 0, K_NO_WAIT);
    if (thread_id_ == nullptr) {
      running_ = false;
      return Status::kUnavailable;
    }
    return Status::kOk;
  }

  void Shutdown() noexcept {
    if (running_) {
      k_thread_abort(thread_id_);
      thread_id_ = nullptr;
      running_ = false;
      k_msgq_purge(&queue_);
    }
  }

  [[nodiscard]] std::string_view Name() const noexcept override { return name_; }

  Status TryPost(WorkItem work, const ExecutionContext& caller) noexcept override {
    return TryPostAt(clock_.NowNs(), work, caller);
  }

  Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                   const ExecutionContext&) noexcept override {
    if (!running_ || !work) {
      return running_ ? Status::kInvalidArgument : Status::kInvalidState;
    }
    const Entry entry{timestamp_ns, work};
    return k_msgq_put(&queue_, &entry, K_NO_WAIT) == 0 ? Status::kOk : Status::kCapacityExceeded;
  }

 private:
  struct Entry {
    std::uint64_t timestamp_ns{};
    WorkItem work{};
  };

  static void EntryPoint(void* self, void*, void*) noexcept {
    static_cast<ThreadExecutor*>(self)->Run();
  }

  void Run() noexcept {
    while (true) {
      Entry entry;
      if (k_msgq_get(&queue_, &entry, K_FOREVER) != 0) {
        continue;
      }
      const auto now = clock_.NowNs();
      if (entry.timestamp_ns > now) {
        const auto delay_ns = entry.timestamp_ns - now;
        k_sleep(K_NSEC(delay_ns));
      }
      entry.work.Run(ExecutionContext(name_, ExecutionKind::kThread, clock_.NowNs()));
    }
  }

  std::string_view name_;
  Clock& clock_;
  k_thread_stack_t* stack_{};
  std::size_t stack_size_{};
  int priority_{};
  struct k_thread thread_ {};
  k_tid_t thread_id_{};
  struct k_msgq queue_ {};
  alignas(4) std::array<char, Capacity * sizeof(Entry)> queue_storage_{};
  bool running_{};
};

}  // namespace aster::platform::zephyr
