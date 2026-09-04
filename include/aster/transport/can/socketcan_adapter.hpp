#pragma once

#include <cstdint>

#include "aster/status.hpp"
#include "aster/transport/can/link.hpp"
#include "aster/transport/can/socketcan.hpp"

namespace aster::transport::can {

class SocketCanAdapter {
 public:
  explicit constexpr SocketCanAdapter(const char* interface_name) noexcept
      : interface_name_(interface_name) {}

  SocketCanAdapter(const SocketCanAdapter&) = delete;
  SocketCanAdapter& operator=(const SocketCanAdapter&) = delete;

  [[nodiscard]] Status Ready() const noexcept {
    return interface_name_ == nullptr || interface_name_[0] == '\0' ? Status::kInvalidArgument
                                                                    : Status::kOk;
  }

  Status Start(CanFrameReceiver receiver) noexcept {
    if (running_ || receiver.receive == nullptr) {
      return running_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    const auto status = socket_.Open(interface_name_);
    if (!IsOk(status)) {
      return status;
    }
    receiver_ = receiver;
    running_ = true;
    return Status::kOk;
  }

  Status Send(const CanFrame& frame, const ExecutionContext& caller) noexcept {
    if (!running_ || caller.kind() == ExecutionKind::kInterrupt) {
      return running_ ? Status::kInvalidArgument : Status::kInvalidState;
    }
    return socket_.Send(frame);
  }

  Status Poll(const ExecutionContext& caller) noexcept {
    if (!running_ || caller.kind() == ExecutionKind::kInterrupt) {
      return running_ ? Status::kInvalidArgument : Status::kInvalidState;
    }
    CanFrame frame;
    const auto status = socket_.Receive(frame);
    return IsOk(status) ? receiver_.Accept(frame, caller.timestamp_ns(), caller) : status;
  }

  Status Stop() noexcept {
    socket_.Close();
    receiver_ = {};
    running_ = false;
    return Status::kOk;
  }

  [[nodiscard]] CanFrameWriter writer() noexcept { return {Write, this}; }
  [[nodiscard]] bool running() const noexcept { return running_; }

 private:
  static Status Write(void* state, const CanFrame& frame, const ExecutionContext& caller) noexcept {
    return state == nullptr ? Status::kInvalidArgument
                            : static_cast<SocketCanAdapter*>(state)->Send(frame, caller);
  }

  const char* interface_name_{};
  SocketCan socket_;
  CanFrameReceiver receiver_{};
  bool running_{};
};

}  // namespace aster::transport::can
