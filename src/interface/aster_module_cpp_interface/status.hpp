#pragma once

#include <cstdint>

#include "aster_module_c_interface/util/status.h"

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

[[nodiscard]] constexpr aster_status_t ToAbiStatus(Status status) noexcept {
  switch (status) {
    case Status::kOk:
    case Status::kInvalidArgument:
    case Status::kNotFound:
    case Status::kCapacityExceeded:
    case Status::kUnavailable:
    case Status::kAlreadyExists:
    case Status::kTimeout:
    case Status::kCancelled:
    case Status::kTypeMismatch:
    case Status::kVersionMismatch:
    case Status::kProtocolError:
    case Status::kInvalidState:
    case Status::kInternal:
      return static_cast<aster_status_t>(status);
  }
  return ASTER_STATUS_INTERNAL;
}

[[nodiscard]] constexpr Status FromAbiStatus(aster_status_t status) noexcept {
  switch (status) {
    case ASTER_STATUS_OK:
    case ASTER_STATUS_INVALID_ARGUMENT:
    case ASTER_STATUS_NOT_FOUND:
    case ASTER_STATUS_CAPACITY_EXCEEDED:
    case ASTER_STATUS_UNAVAILABLE:
    case ASTER_STATUS_ALREADY_EXISTS:
    case ASTER_STATUS_TIMEOUT:
    case ASTER_STATUS_CANCELLED:
    case ASTER_STATUS_TYPE_MISMATCH:
    case ASTER_STATUS_VERSION_MISMATCH:
    case ASTER_STATUS_PROTOCOL_ERROR:
    case ASTER_STATUS_INVALID_STATE:
    case ASTER_STATUS_INTERNAL:
      return static_cast<Status>(status);
    default:
      return Status::kInternal;
  }
}

[[nodiscard]] constexpr StatusCategory CategoryOf(Status status) noexcept {
  return static_cast<StatusCategory>((static_cast<std::uint32_t>(status) >> 24U) & 0xffU);
}

}  // namespace aster
