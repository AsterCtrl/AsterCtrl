#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "uart.hpp"
#include "xrobot/runtime/byte_reader.hpp"

namespace xrobot::backend::libxr {

struct UartReaderAdapterStats {
  std::uint32_t read_calls{};
  std::uint32_t bytes_read{};
  std::uint32_t empty_reads{};
  std::uint32_t failures{};
  std::uint32_t discards{};
};

class UartReaderAdapter final : public xrobot::runtime::ByteReader {
 public:
  explicit UartReaderAdapter(LibXR::UART& uart) noexcept : uart_(uart) {}

  xrobot::runtime::Status Read(
      std::span<std::byte> destination, std::size_t& bytes_read,
      const xrobot::runtime::ExecutionContext& caller) noexcept override {
    using xrobot::runtime::ExecutionKind;
    using xrobot::runtime::Status;

    bytes_read = 0;
    if (destination.empty() || caller.kind() == ExecutionKind::kInterrupt) {
      ++stats_.failures;
      return Status::kInvalidArgument;
    }
    if (uart_.read_port_ == nullptr) {
      ++stats_.failures;
      return Status::kInvalidState;
    }

    ++stats_.read_calls;
    const std::size_t available = uart_.read_port_->Size();
    if (available == 0U) {
      ++stats_.empty_reads;
      return Status::kUnavailable;
    }
    const std::size_t count = std::min(destination.size(), available);
    LibXR::ReadOperation operation;
    const auto error = uart_.Read(
        {static_cast<void*>(destination.data()), count}, operation, false);
    const auto status = MapError(error);
    if (status != Status::kOk) {
      ++stats_.failures;
      return status;
    }
    bytes_read = count;
    stats_.bytes_read += static_cast<std::uint32_t>(count);
    return Status::kOk;
  }

  xrobot::runtime::Status Discard(
      const xrobot::runtime::ExecutionContext& caller) noexcept override {
    using xrobot::runtime::ExecutionKind;
    using xrobot::runtime::Status;

    if (caller.kind() == ExecutionKind::kInterrupt) {
      ++stats_.failures;
      return Status::kInvalidArgument;
    }
    if (uart_.read_port_ == nullptr) {
      ++stats_.failures;
      return Status::kInvalidState;
    }
    const auto status = MapError(uart_.read_port_->ClearQueuedData(false));
    if (status == Status::kOk) {
      ++stats_.discards;
    } else {
      ++stats_.failures;
    }
    return status;
  }

  const UartReaderAdapterStats& stats() const noexcept { return stats_; }

 private:
  static constexpr xrobot::runtime::Status MapError(
      LibXR::ErrorCode error) noexcept {
    using xrobot::runtime::Status;

    switch (error) {
      case LibXR::ErrorCode::OK:
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
      case LibXR::ErrorCode::PENDING:
      case LibXR::ErrorCode::FAILED:
      case LibXR::ErrorCode::CHECK_ERR:
      case LibXR::ErrorCode::NOT_SUPPORT:
        return Status::kInternal;
    }
    return Status::kInternal;
  }

  LibXR::UART& uart_;
  UartReaderAdapterStats stats_{};
};

}  // namespace xrobot::backend::libxr
