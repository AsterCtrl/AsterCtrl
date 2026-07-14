#pragma once

#include <cstdint>

namespace xrobot::runtime {

enum class Status : std::uint8_t {
  kOk = 0,
  kInvalidArgument,
  kInvalidState,
  kCapacityExceeded,
  kUnavailable,
  kTimeout,
  kCancelled,
  kTypeMismatch,
  kInternal,
};

constexpr bool IsOk(Status status) noexcept { return status == Status::kOk; }

}  // namespace xrobot::runtime
