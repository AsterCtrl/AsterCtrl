#include "aster/transport/peer_registry.hpp"

#include <array>
#include <cassert>
#include <cstddef>

namespace {

aster::transport::DeploymentId Deployment(std::byte seed) {
  aster::transport::DeploymentId value;
  value.bytes.fill(seed);
  return value;
}

aster::SchemaHash Schema(std::byte seed) {
  aster::SchemaHash value;
  value.bytes.fill(seed);
  return value;
}

}  // namespace

int main() {
  using aster::Status;
  using aster::transport::PeerHello;
  const auto deployment = Deployment(std::byte{0x42});
  const auto schema = Schema(std::byte{0x24});
  aster::transport::StaticPeerRegistry<2> peers(deployment);

  assert(peers.Register({7, schema, 100}) == Status::kOk);
  assert(peers.Register({7, schema, 100}) == Status::kAlreadyExists);
  assert(peers.Seal() == Status::kOk);

  const PeerHello hello{aster::transport::kPeerProtocolVersion, 7, deployment, schema};
  std::array<std::byte, aster::transport::kPeerHelloEncodedSize> encoded{};
  assert(aster::transport::EncodePeerHello(hello, encoded) == Status::kOk);
  PeerHello decoded;
  assert(aster::transport::DecodePeerHello(encoded, decoded) == Status::kOk);
  assert(decoded.node_id == hello.node_id);
  assert(decoded.deployment_id == hello.deployment_id);
  assert(decoded.schema_hash == hello.schema_hash);

  assert(peers.Observe(decoded, 1'000) == Status::kOk);
  assert(peers.Alive(7, 1'100));
  assert(!peers.Alive(7, 1'101));
  assert(!peers.AllAlive(1'101));

  auto unknown = decoded;
  unknown.node_id = 8;
  assert(peers.Observe(unknown, 1'200) == Status::kNotFound);
  auto wrong_deployment = decoded;
  wrong_deployment.deployment_id = Deployment(std::byte{0x11});
  assert(peers.Observe(wrong_deployment, 1'200) == Status::kVersionMismatch);
  auto wrong_schema = decoded;
  wrong_schema.schema_hash = Schema(std::byte{0x11});
  assert(peers.Observe(wrong_schema, 1'200) == Status::kTypeMismatch);
  assert(peers.size() == 1);
}
