#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include "aster/transport/usb/stream_transport.hpp"

namespace {

class LoopbackStream final : public aster::transport::usb::ByteStream {
 public:
  aster::Status Write(std::span<const std::byte> input, std::size_t& written) noexcept override {
    if (input.size() > buffer_.size()) {
      written = 0;
      return aster::Status::kCapacityExceeded;
    }
    std::copy(input.begin(), input.end(), buffer_.begin());
    size_ = input.size();
    written = input.size();
    return aster::Status::kOk;
  }

  aster::Status Read(std::span<std::byte> output, std::size_t& read) noexcept override {
    if (size_ == 0) {
      read = 0;
      return aster::Status::kUnavailable;
    }
    read = std::min(output.size(), size_);
    std::copy_n(buffer_.begin(), read, output.begin());
    std::move(buffer_.begin() + static_cast<std::ptrdiff_t>(read),
              buffer_.begin() + static_cast<std::ptrdiff_t>(size_), buffer_.begin());
    size_ -= read;
    return aster::Status::kOk;
  }

  void Close() noexcept override { size_ = 0; }

 private:
  std::array<std::byte, 256> buffer_{};
  std::size_t size_{};
};

struct Capture {
  std::uint16_t route{};
  std::array<std::byte, 16> payload{};
  std::size_t size{};
};

aster::Status Receive(void* state, const aster::transport::PacketView& packet,
                      const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  capture.route = packet.header.route_id;
  capture.size = packet.payload.size();
  std::copy(packet.payload.begin(), packet.payload.end(), capture.payload.begin());
  return aster::Status::kOk;
}

void SendsAndReceivesOneFramedPacket() {
  LoopbackStream stream;
  aster::transport::usb::StreamTransport<16> transport(stream);
  Capture capture;
  assert(transport.Start(Receive, &capture) == aster::Status::kOk);
  const std::array payload{std::byte{1}, std::byte{0}, std::byte{3}};
  aster::SchemaHash schema{};
  const aster::transport::PacketView packet{
      {8, aster::transport::PacketKind::kChannel, 4, 10, 20, schema}, payload};
  const aster::ExecutionContext context("usb", aster::ExecutionKind::kThread, 10);
  assert(transport.Send(packet, context) == aster::Status::kOk);
  assert(transport.Poll(context) == aster::Status::kOk);
  assert(capture.route == 8);
  assert(capture.size == payload.size());
  assert(std::equal(payload.begin(), payload.end(), capture.payload.begin()));
  assert(transport.stats().packets_sent == 1);
  assert(transport.stats().packets_received == 1);
}

void RejectsMalformedWireAsAProtocolError() {
  LoopbackStream stream;
  aster::transport::usb::StreamTransport<16> transport(stream);
  Capture capture;
  assert(transport.Start(Receive, &capture) == aster::Status::kOk);
  constexpr std::array malformed_frame{std::byte{2}, std::byte{0}};
  std::size_t written{};
  assert(stream.Write(malformed_frame, written) == aster::Status::kOk);
  const aster::ExecutionContext context("usb", aster::ExecutionKind::kThread, 10);
  assert(transport.Poll(context) == aster::Status::kProtocolError);
  assert(transport.stats().invalid_packets == 1);
  assert(transport.stats().packets_received == 0);
}

}  // namespace

int main() {
  SendsAndReceivesOneFramedPacket();
  RejectsMalformedWireAsAProtocolError();
  return 0;
}
