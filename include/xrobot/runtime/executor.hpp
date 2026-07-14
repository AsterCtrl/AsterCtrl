#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "xrobot/runtime/execution_context.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::runtime {

enum class ExecutorState : std::uint8_t {
  kConstructed,
  kInitialized,
  kRunning,
  kStopped,
  kFailed,
};

struct ExecutorStats {
  std::uint32_t accepted{};
  std::uint32_t executed{};
  std::uint32_t rejected{};
  std::size_t high_watermark{};
};

using WorkCallback = void (*)(void*, const ExecutionContext&) noexcept;

struct WorkItem {
  WorkCallback callback{};
  void* state{};

  constexpr explicit operator bool() const noexcept {
    return callback != nullptr;
  }

  void Run(const ExecutionContext& context) const noexcept {
    callback(state, context);
  }
};

class Executor {
 public:
  virtual ~Executor() = default;

  virtual std::string_view Name() const noexcept = 0;
  virtual const ExecutionContext& context() const noexcept = 0;
  virtual ExecutorState state() const noexcept = 0;
  virtual ExecutorStats stats() const noexcept = 0;

  virtual Status Initialize() noexcept = 0;
  virtual Status Start() noexcept = 0;
  virtual Status TryPost(WorkItem work,
                         const ExecutionContext& caller) noexcept = 0;
  virtual void Shutdown() noexcept = 0;
};

struct ExecutorSlot {
  Executor* executor{};
};

}  // namespace xrobot::runtime
