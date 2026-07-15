#pragma once

#include "libxr_def.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::backend::libxr {

constexpr xrobot::runtime::Status MapError(LibXR::ErrorCode error) noexcept {
  using xrobot::runtime::Status;

  switch (error) {
    case LibXR::ErrorCode::OK:
    case LibXR::ErrorCode::PENDING:
      return Status::kOk;
    case LibXR::ErrorCode::ARG_ERR:
    case LibXR::ErrorCode::SIZE_ERR:
    case LibXR::ErrorCode::PTR_NULL:
    case LibXR::ErrorCode::OUT_OF_RANGE:
      return Status::kInvalidArgument;
    case LibXR::ErrorCode::INIT_ERR:
    case LibXR::ErrorCode::STATE_ERR:
      return Status::kInvalidState;
    case LibXR::ErrorCode::NO_MEM:
    case LibXR::ErrorCode::NO_BUFF:
    case LibXR::ErrorCode::FULL:
      return Status::kCapacityExceeded;
    case LibXR::ErrorCode::NO_RESPONSE:
    case LibXR::ErrorCode::TIMEOUT:
      return Status::kTimeout;
    case LibXR::ErrorCode::NOT_FOUND:
    case LibXR::ErrorCode::EMPTY:
    case LibXR::ErrorCode::BUSY:
      return Status::kUnavailable;
    case LibXR::ErrorCode::FAILED:
    case LibXR::ErrorCode::CHECK_ERR:
    case LibXR::ErrorCode::NOT_SUPPORT:
      return Status::kInternal;
  }
  return Status::kInternal;
}

}  // namespace xrobot::backend::libxr
