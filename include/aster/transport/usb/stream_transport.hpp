#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/status.hpp"
#include "aster/transport/transport.hpp"
#include "aster/transport/usb/byte_stream.hpp"
#include "aster/transport/usb/framing.hpp"

namespace aster::transport::usb {

template <std::size_t MaxPayload>
class StreamTransport final : public Transport {
 public:
  explicit StreamTransport(ByteStream& stream) noexcept : stream_(stream) {}

  Status Start(PacketReceiver receiver, void* receiver_state) noexcept override {
    if (receiver == nullptr || receiver_ != nullptr) {
      return receiver == nullptr ? Status::kInvalidArgument : Status::kInvalidState;
    }
    receiver_ = receiver;
    receiver_state_ = receiver_state;
    return Status::kOk;
  }

  Status Send(const PacketView& packet, const ExecutionContext&) noexcept override {
    if (receiver_ == nullptr) {
      return Status::kInvalidState;
    }
    std::size_t encoded_size{};
    const auto encode_status = codec_.Encode(packet, encoded_, encoded_size);
    if (!IsOk(encode_status)) {
      ++stats_.invalid_packets;
      return encode_status;
    }
    std::size_t written{};
    const auto status =
        stream_.Write(std::span<const std::byte>(encoded_).first(encoded_size), written);
    if (!IsOk(status) || written != encoded_size) {
      ++stats_.backpressure;
      return IsOk(status) ? Status::kUnavailable : status;
    }
    ++stats_.packets_sent;
    stats_.bytes_sent += static_cast<std::uint32_t>(packet.payload.size());
    return Status::kOk;
  }

  Status Poll(const ExecutionContext& caller) noexcept override {
    if (receiver_ == nullptr) {
      return Status::kInvalidState;
    }
    std::array<std::byte, 64> chunk{};
    std::size_t read{};
    const auto read_status = stream_.Read(chunk, read);
    if (!IsOk(read_status)) {
      return read_status;
    }
    if (read == 0) {
      return Status::kUnavailable;
    }
    for (std::size_t index = 0; index < read; ++index) {
      const auto value = chunk[index];
      if (value != std::byte{0}) {
        if (received_size_ + 1U >= received_.size()) {
          received_size_ = 0;
          ++stats_.invalid_packets;
          return Status::kCapacityExceeded;
        }
        received_[received_size_++] = value;
        continue;
      }
      if (received_size_ == 0) {
        continue;
      }
      received_[received_size_++] = std::byte{0};
      PacketView packet;
      const auto decode_status =
          codec_.Decode(std::span<const std::byte>(received_).first(received_size_), packet);
      received_size_ = 0;
      if (!IsOk(decode_status)) {
        ++stats_.invalid_packets;
        return decode_status;
      }
      const auto receive_status = receiver_(receiver_state_, packet, caller);
      if (!IsOk(receive_status)) {
        ++stats_.invalid_packets;
        return receive_status;
      }
      ++stats_.packets_received;
      stats_.bytes_received += static_cast<std::uint32_t>(packet.payload.size());
    }
    return Status::kOk;
  }

  void Stop() noexcept override {
    receiver_ = nullptr;
    receiver_state_ = nullptr;
    received_size_ = 0;
    stream_.Close();
  }

  [[nodiscard]] TransportStats stats() const noexcept override { return stats_; }

 private:
  ByteStream& stream_;
  PacketCodec<MaxPayload> codec_;
  std::array<std::byte, PacketCodec<MaxPayload>::kEncodedCapacity> encoded_{};
  std::array<std::byte, PacketCodec<MaxPayload>::kEncodedCapacity> received_{};
  std::size_t received_size_{};
  PacketReceiver receiver_{};
  void* receiver_state_{};
  TransportStats stats_{};
};

}  // namespace aster::transport::usb
