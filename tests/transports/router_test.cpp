#include "aster/transport/router.hpp"

#include <array>
#include <cassert>
#include <cstddef>

namespace {

struct Capture {
  std::size_t calls{};
  std::byte first{};
};

aster::Status CapturePacket(void* state, const aster::transport::PacketView& packet,
                            const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  ++capture.calls;
  capture.first = packet.payload.empty() ? std::byte{} : packet.payload.front();
  return aster::Status::kOk;
}

void RoutesOnlyExpectedIdentityAndSchema() {
  aster::transport::StaticRouter<2> router;
  Capture capture;
  aster::SchemaHash schema{};
  schema.bytes[0] = std::byte{0x42};
  assert(router.Register(9, aster::transport::PacketKind::kChannel, schema, CapturePacket,
                         &capture) == aster::Status::kOk);
  assert(router.Register(9, aster::transport::PacketKind::kChannel, schema, CapturePacket,
                         &capture) == aster::Status::kAlreadyExists);
  assert(router.Seal() == aster::Status::kOk);

  const std::array payload{std::byte{0x7a}};
  const aster::ExecutionContext context("transport", aster::ExecutionKind::kThread, 10);
  aster::transport::PacketView packet{{9, aster::transport::PacketKind::kChannel, 1, 0, 0, schema},
                                      payload};
  assert(router.Accept(packet, context) == aster::Status::kOk);
  assert(capture.calls == 1);
  assert(capture.first == payload[0]);

  packet.header.schema_hash.bytes[0] = std::byte{0x43};
  assert(router.Accept(packet, context) == aster::Status::kTypeMismatch);
  packet.header.schema_hash = schema;
  packet.header.route_id = 10;
  assert(router.Accept(packet, context) == aster::Status::kNotFound);
}

}  // namespace

int main() {
  RoutesOnlyExpectedIdentityAndSchema();
  return 0;
}
