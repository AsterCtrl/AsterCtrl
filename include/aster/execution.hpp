#pragma once

#include <cstdint>
#include <string_view>

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

using WorkCallback = void (*)(void*, const ExecutionContext&) noexcept;

struct WorkItem {
  WorkCallback callback{};
  void* state{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return callback != nullptr; }

  void Run(const ExecutionContext& context) const noexcept {
    if (callback != nullptr) {
      callback(state, context);
    }
  }
};

}  // namespace aster
