#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "xrobot/runtime/execution_context.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::runtime {

template <typename Value>
concept ParameterValue =
    (std::is_arithmetic_v<Value> || std::is_enum_v<Value>) &&
    std::is_trivially_copyable_v<Value>;

enum class ParameterMutability : std::uint8_t {
  kBuildTime,
  kStartup,
  kRuntime,
};

enum class ParameterPersistence : std::uint8_t {
  kCompiled,
  kVolatile,
  kPersistent,
};

enum class ParameterWritePhase : std::uint8_t {
  kStartup,
  kRuntime,
};

template <ParameterValue Value>
struct ParameterDescriptor {
  std::string_view name;
  std::string_view unit;
  Value default_value{};
  Value minimum{};
  Value maximum{};
  ParameterMutability mutability{ParameterMutability::kBuildTime};
  ParameterPersistence persistence{ParameterPersistence::kCompiled};
};

struct ParameterStats {
  std::uint32_t updates{};
  std::uint32_t rejected{};
};

template <ParameterValue Value>
class Parameter {
 public:
  using UpdateCallback = Status (*)(void*, Value, Value,
                                    ParameterWritePhase,
                                    const ExecutionContext&) noexcept;

  constexpr explicit Parameter(ParameterDescriptor<Value> descriptor,
                               UpdateCallback callback = nullptr,
                               void* callback_state = nullptr) noexcept
      : descriptor_(descriptor),
        value_(descriptor.default_value),
        callback_(callback),
        callback_state_(callback_state) {}

  Status Set(Value candidate, ParameterWritePhase phase,
             const ExecutionContext& caller) noexcept {
    if (caller.kind() == ExecutionKind::kInterrupt) {
      ++stats_.rejected;
      return Status::kInvalidArgument;
    }
    if (!CanWrite(phase)) {
      ++stats_.rejected;
      return Status::kInvalidState;
    }
    if (candidate < descriptor_.minimum || candidate > descriptor_.maximum) {
      ++stats_.rejected;
      return Status::kInvalidArgument;
    }
    if (callback_ != nullptr) {
      const auto status =
          callback_(callback_state_, value_, candidate, phase, caller);
      if (!IsOk(status)) {
        ++stats_.rejected;
        return status;
      }
    }

    value_ = candidate;
    ++revision_;
    ++stats_.updates;
    return Status::kOk;
  }

  constexpr void SealStartup() noexcept { startup_sealed_ = true; }
  constexpr Value value() const noexcept { return value_; }
  constexpr std::uint32_t revision() const noexcept { return revision_; }
  constexpr bool startup_sealed() const noexcept { return startup_sealed_; }
  constexpr const ParameterDescriptor<Value>& descriptor() const noexcept {
    return descriptor_;
  }
  constexpr const ParameterStats& stats() const noexcept { return stats_; }

 private:
  constexpr bool CanWrite(ParameterWritePhase phase) const noexcept {
    if (descriptor_.mutability == ParameterMutability::kBuildTime) {
      return false;
    }
    if (phase == ParameterWritePhase::kStartup) {
      return !startup_sealed_;
    }
    return startup_sealed_ &&
           descriptor_.mutability == ParameterMutability::kRuntime;
  }

  ParameterDescriptor<Value> descriptor_;
  Value value_;
  UpdateCallback callback_;
  void* callback_state_;
  std::uint32_t revision_{};
  bool startup_sealed_{};
  ParameterStats stats_{};
};

}  // namespace xrobot::runtime
