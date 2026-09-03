#include <array>
#include <cassert>
#include <cstddef>

#include "aster/transport/local.hpp"

namespace {

struct Capture {
  std::size_t calls{};
  std::uint16_t route{};
};

aster::Status Receive(void* state, const aster::transport::PacketView& packet,
                      const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  ++capture.calls;
  capture.route = packet.header.route_id;
  return aster::Status::kOk;
}

void DeliversWithoutSerialization() {
  aster::transport::LocalTransport transport;
  Capture capture;
  assert(transport.Start(Receive, &capture) == aster::Status::kOk);
  const std::array payload{std::byte{1}, std::byte{2}};
  aster::SchemaHash schema{};
  const aster::transport::PacketView packet{
      {7, aster::transport::PacketKind::kChannel, 1, 2, 3, schema}, payload};
  const aster::ExecutionContext context("local", aster::ExecutionKind::kThread, 2);
  assert(transport.Send(packet, context) == aster::Status::kOk);
  assert(capture.calls == 1);
  assert(capture.route == 7);
  assert(transport.stats().packets_sent == 1);
  assert(transport.stats().packets_received == 1);
  transport.Stop();
  assert(transport.Send(packet, context) == aster::Status::kInvalidState);
}

}  // namespace

int main() {
  DeliversWithoutSerialization();
  return 0;
}
