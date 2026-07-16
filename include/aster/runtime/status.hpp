#pragma once

#include <cstdint>

namespace aster::runtime {

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

}  // namespace aster::runtime
