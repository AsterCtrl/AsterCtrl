#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/registry.hpp"
#include "aster/status.hpp"
#include "aster/type_support.hpp"

namespace aster::transport {

inline constexpr std::uint16_t kPeerProtocolVersion = 1;

struct DeploymentId {
  std::array<std::byte, 32> bytes{};

  constexpr bool operator==(const DeploymentId&) const noexcept = default;
};

struct PeerHello {
  std::uint16_t protocol_version{kPeerProtocolVersion};
  std::uint16_t node_id{};
  DeploymentId deployment_id;
  SchemaHash schema_hash;
};

inline constexpr std::size_t kPeerHelloEncodedSize = 52;

inline Status EncodePeerHello(const PeerHello& hello,
                              std::span<std::byte, kPeerHelloEncodedSize> output) noexcept {
  if (hello.protocol_version == 0 || hello.node_id == 0) {
    return Status::kInvalidArgument;
  }
  output[0] = static_cast<std::byte>(hello.protocol_version & 0xffU);
  output[1] = static_cast<std::byte>(hello.protocol_version >> 8U);
  output[2] = static_cast<std::byte>(hello.node_id & 0xffU);
  output[3] = static_cast<std::byte>(hello.node_id >> 8U);
  std::copy(hello.deployment_id.bytes.begin(), hello.deployment_id.bytes.end(), output.begin() + 4);
  std::copy(hello.schema_hash.bytes.begin(), hello.schema_hash.bytes.end(), output.begin() + 36);
  return Status::kOk;
}

inline Status DecodePeerHello(std::span<const std::byte> input, PeerHello& hello) noexcept {
  if (input.size() != kPeerHelloEncodedSize) {
    return Status::kInvalidArgument;
  }
  hello.protocol_version = static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(input[0]) |
      (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[1])) << 8U));
  hello.node_id = static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(input[2]) |
      (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[3])) << 8U));
  std::copy(input.begin() + 4, input.begin() + 36, hello.deployment_id.bytes.begin());
  std::copy(input.begin() + 36, input.end(), hello.schema_hash.bytes.begin());
  return hello.protocol_version == 0 || hello.node_id == 0 ? Status::kInvalidArgument : Status::kOk;
}

struct ExpectedPeer {
  std::uint16_t node_id{};
  SchemaHash schema_hash;
  std::uint64_t timeout_ns{};
};

template <std::size_t MaxPeers>
class StaticPeerRegistry final : public Registry {
 public:
  static_assert(MaxPeers > 0);

  explicit constexpr StaticPeerRegistry(DeploymentId deployment_id) noexcept
      : deployment_id_(deployment_id) {}

  Status Register(ExpectedPeer peer) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (peer.node_id == 0 || peer.timeout_ns == 0) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (peers_[index].expected.node_id == peer.node_id) {
        return Status::kAlreadyExists;
      }
    }
    if (size_ == peers_.size()) {
      return Status::kCapacityExceeded;
    }
    peers_[size_++].expected = peer;
    return Status::kOk;
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }

  Status Observe(const PeerHello& hello, std::uint64_t receive_time_ns) noexcept {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    if (hello.protocol_version != kPeerProtocolVersion || hello.deployment_id != deployment_id_) {
      return Status::kVersionMismatch;
    }
    auto* peer = Find(hello.node_id);
    if (peer == nullptr) {
      return Status::kNotFound;
    }
    if (peer->expected.schema_hash != hello.schema_hash) {
      return Status::kTypeMismatch;
    }
    peer->last_seen_ns = receive_time_ns;
    peer->seen = true;
    return Status::kOk;
  }

  [[nodiscard]] bool Alive(std::uint16_t node_id, std::uint64_t now_ns) const noexcept {
    const auto* peer = Find(node_id);
    return peer != nullptr && peer->seen && now_ns >= peer->last_seen_ns &&
           now_ns - peer->last_seen_ns <= peer->expected.timeout_ns;
  }

  [[nodiscard]] bool AllAlive(std::uint64_t now_ns) const noexcept {
    if (!sealed_) {
      return false;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (!Alive(peers_[index].expected.node_id, now_ns)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

 private:
  struct PeerState {
    ExpectedPeer expected{};
    std::uint64_t last_seen_ns{};
    bool seen{};
  };

  [[nodiscard]] PeerState* Find(std::uint16_t node_id) noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
      if (peers_[index].expected.node_id == node_id) {
        return &peers_[index];
      }
    }
    return nullptr;
  }

  [[nodiscard]] const PeerState* Find(std::uint16_t node_id) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
      if (peers_[index].expected.node_id == node_id) {
        return &peers_[index];
      }
    }
    return nullptr;
  }

  DeploymentId deployment_id_;
  std::array<PeerState, MaxPeers> peers_{};
  std::size_t size_{};
  bool sealed_{};
};

}  // namespace aster::transport
