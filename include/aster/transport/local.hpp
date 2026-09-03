#pragma once

#include "aster/status.hpp"
#include "aster/transport/transport.hpp"

namespace aster::transport {

class LocalTransport final : public Transport {
 public:
  Status Start(PacketReceiver receiver, void* receiver_state) noexcept override {
    if (receiver == nullptr || receiver_ != nullptr) {
      return receiver == nullptr ? Status::kInvalidArgument : Status::kInvalidState;
    }
    receiver_ = receiver;
    receiver_state_ = receiver_state;
    return Status::kOk;
  }

  Status Send(const PacketView& packet, const ExecutionContext& caller) noexcept override {
    if (receiver_ == nullptr) {
      return Status::kInvalidState;
    }
    ++stats_.packets_sent;
    stats_.bytes_sent += static_cast<std::uint32_t>(packet.payload.size());
    const auto status = receiver_(receiver_state_, packet, caller);
    if (IsOk(status)) {
      ++stats_.packets_received;
      stats_.bytes_received += static_cast<std::uint32_t>(packet.payload.size());
    } else {
      ++stats_.invalid_packets;
    }
    return status;
  }

  Status Poll(const ExecutionContext&) noexcept override {
    return receiver_ == nullptr ? Status::kInvalidState : Status::kUnavailable;
  }

  void Stop() noexcept override {
    receiver_ = nullptr;
    receiver_state_ = nullptr;
  }

  [[nodiscard]] TransportStats stats() const noexcept override { return stats_; }

 private:
  PacketReceiver receiver_{};
  void* receiver_state_{};
  TransportStats stats_{};
};

}  // namespace aster::transport
