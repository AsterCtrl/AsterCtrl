#pragma once

#include <cstdint>

#include "aster/execution.hpp"
#include "aster/transport/can/protocol.hpp"

namespace aster::transport::can {

using CanFrameWrite = Status (*)(void*, const CanFrame&, const aster::ExecutionContext&) noexcept;

struct CanFrameWriter {
  CanFrameWrite write{};
  void* state{};

  Status Send(const CanFrame& frame, const aster::ExecutionContext& context) const noexcept {
    auto wire_frame = frame;
    wire_frame.write_completion = nullptr;
    wire_frame.write_state = nullptr;
    wire_frame.write_token = 0;
    const auto status = write == nullptr ? Status::kUnavailable : write(state, wire_frame, context);
    frame.CompleteWrite(status);
    return status;
  }
};

using CanClockRead = std::uint64_t (*)(void*) noexcept;

struct CanClockReader {
  CanClockRead read{};
  void* state{};

  std::uint64_t NowNs() const noexcept { return read == nullptr ? 0 : read(state); }
};

using CanTimeConvert = std::uint64_t (*)(void*, std::uint64_t) noexcept;

struct CanTimeConverter {
  CanTimeConvert convert{};
  void* state{};

  std::uint64_t ToNetworkTime(std::uint64_t local_time_ns) const noexcept {
    return convert == nullptr ? local_time_ns : convert(state, local_time_ns);
  }
};

using CanFrameReceive = Status (*)(void*, const CanFrame&, std::uint64_t,
                                   const aster::ExecutionContext&) noexcept;

struct CanFrameReceiver {
  CanFrameReceive receive{};
  void* state{};

  Status Accept(const CanFrame& frame, std::uint64_t receive_time_ns,
                const aster::ExecutionContext& context) const noexcept {
    return receive == nullptr ? Status::kUnavailable
                              : receive(state, frame, receive_time_ns, context);
  }
};

}  // namespace aster::transport::can
