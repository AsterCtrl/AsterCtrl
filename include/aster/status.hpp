#pragma once

#include <cstdint>

namespace aster {

enum class StatusCategory : std::uint8_t {
  kOk = 0,
  kConfiguration = 1,
  kResource = 2,
  kTimeout = 3,
  kProtocol = 4,
  kLifecycle = 5,
  kPlatform = 6,
};

enum class Status : std::int32_t {
  kOk = 0,
  kInvalidArgument = 0x01000001,
  kNotFound = 0x01000002,
  kCapacityExceeded = 0x02000001,
  kUnavailable = 0x02000002,
  kAlreadyExists = 0x02000003,
  kTimeout = 0x03000001,
  kCancelled = 0x03000002,
  kTypeMismatch = 0x04000001,
  kVersionMismatch = 0x04000002,
  kProtocolError = 0x04000003,
  kInvalidState = 0x05000001,
  kInternal = 0x06000001,
};

[[nodiscard]] constexpr bool IsOk(Status status) noexcept { return status == Status::kOk; }

[[nodiscard]] constexpr StatusCategory CategoryOf(Status status) noexcept {
  return static_cast<StatusCategory>((static_cast<std::uint32_t>(status) >> 24U) & 0xffU);
}

}  // namespace aster
