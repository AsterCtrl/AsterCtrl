#include "aster/transport/channel_bridge.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "aster/channel.hpp"
#include "aster/transport/local.hpp"

namespace {

constexpr aster::SchemaHash Schema() {
  aster::SchemaHash hash{};
  hash.bytes[0] = std::byte{0x52};
  return hash;
}

constexpr aster::ChannelDescriptor Descriptor() {
  return {"/command", {"example.Command", Schema(), 8}};
}

struct Capture {
  std::array<std::byte, 8> bytes{};
  std::size_t size{};
  std::uint64_t source_timestamp_ns{};
};

aster::Status Receive(void* state, std::span<const std::byte> bytes, const aster::MessageInfo& info,
                      const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  capture.size = bytes.size();
  capture.source_timestamp_ns = info.source_timestamp_ns;
  std::copy(bytes.begin(), bytes.end(), capture.bytes.begin());
  return aster::Status::kOk;
}

void BridgesChannelThroughTheTransportInterface() {
  aster::LocalChannel<1, 1, 8> source;
  aster::LocalChannel<1, 1, 8> destination;
  aster::transport::LocalTransport transport;
  aster::transport::StaticRouter<1> router;
  aster::transport::ChannelPacketEgress egress(8, transport, 100);
  aster::transport::ChannelPacketIngress ingress(8);
  Capture capture;

  assert(source.RegisterPublisher(Descriptor()) == aster::Status::kOk);
  assert(egress.Bind(aster::ChannelRef(source), Descriptor()) == aster::Status::kOk);
  assert(ingress.Bind(aster::ChannelRef(destination), router, Descriptor()) == aster::Status::kOk);
  assert(destination.RegisterSubscriber(Descriptor(), Receive, &capture) == aster::Status::kOk);
  assert(source.Seal() == aster::Status::kOk);
  assert(destination.Seal() == aster::Status::kOk);
  assert(router.Seal() == aster::Status::kOk);
  assert(transport.Start(decltype(router)::Receive, &router) == aster::Status::kOk);

  const std::array message{std::byte{1}, std::byte{0}, std::byte{3}};
  const aster::ExecutionContext context("transport", aster::ExecutionKind::kThread, 120);
  assert(source.Publish(Descriptor(), message, 100, context) == aster::Status::kOk);
  assert(capture.size == message.size());
  assert(std::equal(message.begin(), message.end(), capture.bytes.begin()));
  assert(capture.source_timestamp_ns == 100);
  assert(egress.stats().messages == 1);
  assert(ingress.stats().messages == 1);
}

void RejectsExpiredPacketsWithoutPublishingThem() {
  aster::LocalChannel<1, 1, 8> source;
  aster::LocalChannel<1, 1, 8> destination;
  aster::transport::LocalTransport transport;
  aster::transport::StaticRouter<1> router;
  aster::transport::ChannelPacketEgress egress(9, transport, 10);
  aster::transport::ChannelPacketIngress ingress(9);
  Capture capture;

  assert(source.RegisterPublisher(Descriptor()) == aster::Status::kOk);
  assert(egress.Bind(aster::ChannelRef(source), Descriptor()) == aster::Status::kOk);
  assert(ingress.Bind(aster::ChannelRef(destination), router, Descriptor()) == aster::Status::kOk);
  assert(destination.RegisterSubscriber(Descriptor(), Receive, &capture) == aster::Status::kOk);
  assert(source.Seal() == aster::Status::kOk);
  assert(destination.Seal() == aster::Status::kOk);
  assert(router.Seal() == aster::Status::kOk);
  assert(transport.Start(decltype(router)::Receive, &router) == aster::Status::kOk);

  const std::array message{std::byte{7}};
  const aster::ExecutionContext context("transport", aster::ExecutionKind::kThread, 111);
  assert(source.Publish(Descriptor(), message, 100, context) == aster::Status::kTimeout);
  assert(capture.size == 0);
  assert(egress.stats().send_failures == 1);
  assert(ingress.stats().expired == 1);
}

}  // namespace

int main() {
  BridgesChannelThroughTheTransportInterface();
  RejectsExpiredPacketsWithoutPublishingThem();
  return 0;
}
