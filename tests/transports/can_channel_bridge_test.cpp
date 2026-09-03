#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "aster/channel.hpp"
#include "aster/transport/can/channel_bridge.hpp"

namespace {

constexpr aster::SchemaHash Schema() {
  aster::SchemaHash hash{};
  hash.bytes[0] = std::byte{0x31};
  return hash;
}

constexpr aster::ChannelDescriptor Descriptor() {
  return {"/state", {"example.State", Schema(), 8}};
}

struct Link {
  std::array<aster::transport::can::CanFrame, 4> frames{};
  std::size_t size{};

  static aster::Status Write(void* state, const aster::transport::can::CanFrame& frame,
                             const aster::ExecutionContext&) noexcept {
    auto& link = *static_cast<Link*>(state);
    if (link.size == link.frames.size()) {
      return aster::Status::kCapacityExceeded;
    }
    link.frames[link.size++] = frame;
    return aster::Status::kOk;
  }
};

struct Capture {
  std::array<std::byte, 8> bytes{};
  std::size_t size{};
};

aster::Status Receive(void* state, std::span<const std::byte> bytes, const aster::MessageInfo&,
                      const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  capture.size = bytes.size();
  std::copy(bytes.begin(), bytes.end(), capture.bytes.begin());
  return aster::Status::kOk;
}

void BridgesBoundedChannelAcrossCan() {
  aster::LocalChannel<2, 2, 8> source;
  aster::LocalChannel<2, 2, 8> destination;
  Link link;
  aster::transport::can::FastChannelEgress<8> egress(9, aster::transport::can::CanPriority::kState,
                                                     {Link::Write, &link});
  aster::transport::can::FastChannelIngress<8> ingress(
      9, {.deadline_ns = 10'000'000, .max_age_ns = 10'000'000});
  Capture capture;

  assert(source.RegisterPublisher(Descriptor()) == aster::Status::kOk);
  assert(egress.Bind(aster::ChannelRef(source), Descriptor()) == aster::Status::kOk);
  assert(ingress.Bind(aster::ChannelRef(destination), Descriptor()) == aster::Status::kOk);
  assert(destination.RegisterSubscriber(Descriptor(), Receive, &capture) == aster::Status::kOk);
  assert(source.Seal() == aster::Status::kOk);
  assert(destination.Seal() == aster::Status::kOk);

  const std::array message{std::byte{1}, std::byte{2}, std::byte{0}, std::byte{4}};
  const aster::ExecutionContext context("can", aster::ExecutionKind::kThread, 1'000'000);
  assert(source.Publish(Descriptor(), message, 1'000'000, context) == aster::Status::kOk);
  assert(link.size > 0);
  for (std::size_t index = 0; index < link.size; ++index) {
    const auto status = ingress.Accept(link.frames[index], 1'000'000, context);
    assert(status == aster::Status::kOk || status == aster::Status::kUnavailable);
  }
  assert(capture.size == message.size());
  assert(std::equal(message.begin(), message.end(), capture.bytes.begin()));
  assert(egress.stats().messages == 1);
  assert(ingress.stats().messages == 1);
}

}  // namespace

int main() {
  BridgesBoundedChannelAcrossCan();
  return 0;
}
