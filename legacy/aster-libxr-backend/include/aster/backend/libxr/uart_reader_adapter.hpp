#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "uart.hpp"
#include "aster/backend/libxr/error.hpp"
#include "aster/backend/libxr/resources.hpp"
#include "aster/runtime/byte_reader.hpp"

namespace aster::backend::libxr {

struct UartReaderAdapterStats {
  std::uint32_t read_calls{};
  std::uint32_t bytes_read{};
  std::uint32_t empty_reads{};
  std::uint32_t failures{};
  std::uint32_t discards{};
};

class UartReaderAdapter final : public aster::runtime::ByteReader {
 public:
  explicit UartReaderAdapter(LibXR::UART& uart) noexcept : uart_(uart) {}
  explicit UartReaderAdapter(UartResource& uart) noexcept : uart_(uart.get()) {}

  aster::runtime::Status Read(
      std::span<std::byte> destination, std::size_t& bytes_read,
      const aster::runtime::ExecutionContext& caller) noexcept override {
    using aster::runtime::ExecutionKind;
    using aster::runtime::Status;

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
    const auto status = aster::backend::libxr::MapError(error);
    if (status != Status::kOk) {
      ++stats_.failures;
      return status;
    }
    bytes_read = count;
    stats_.bytes_read += static_cast<std::uint32_t>(count);
    return Status::kOk;
  }

  aster::runtime::Status Discard(
      const aster::runtime::ExecutionContext& caller) noexcept override {
    using aster::runtime::ExecutionKind;
    using aster::runtime::Status;

    if (caller.kind() == ExecutionKind::kInterrupt) {
      ++stats_.failures;
      return Status::kInvalidArgument;
    }
    if (uart_.read_port_ == nullptr) {
      ++stats_.failures;
      return Status::kInvalidState;
    }
    const auto status = aster::backend::libxr::MapError(
        uart_.read_port_->ClearQueuedData(false));
    if (status == Status::kOk) {
      ++stats_.discards;
    } else {
      ++stats_.failures;
    }
    return status;
  }

  const UartReaderAdapterStats& stats() const noexcept { return stats_; }

 private:
  LibXR::UART& uart_;
  UartReaderAdapterStats stats_{};
};

}  // namespace aster::backend::libxr
