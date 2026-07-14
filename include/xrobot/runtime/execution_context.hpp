#pragma once

#include <cstdint>
#include <string_view>

namespace xrobot::runtime {

enum class ExecutionKind : std::uint8_t {
  kThread,
  kCallback,
  kInterrupt,
};

class ExecutionContext {
 public:
  constexpr ExecutionContext(std::string_view executor_name,
                             ExecutionKind kind,
                             std::uint8_t priority) noexcept
      : executor_name_(executor_name), kind_(kind), priority_(priority) {}

  constexpr std::string_view executor_name() const noexcept {
    return executor_name_;
  }
  constexpr ExecutionKind kind() const noexcept { return kind_; }
  constexpr std::uint8_t priority() const noexcept { return priority_; }
  constexpr bool blocking_allowed() const noexcept {
    return kind_ == ExecutionKind::kThread;
  }

 private:
  std::string_view executor_name_;
  ExecutionKind kind_;
  std::uint8_t priority_;
};

}  // namespace xrobot::runtime
