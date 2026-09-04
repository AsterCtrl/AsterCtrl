#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/channel.hpp"
#include "aster/status.hpp"
#include "aster/transport/router.hpp"
#include "aster/transport/transport.hpp"

namespace aster::transport {

struct ChannelPacketEgressStats {
  std::uint32_t messages{};
  std::uint32_t send_failures{};
};

class ChannelPacketEgress {
 public:
  constexpr ChannelPacketEgress(std::uint16_t route_id, Transport& transport,
                                std::uint64_t maximum_age_ns = 0) noexcept
      : route_id_(route_id), transport_(transport), maximum_age_ns_(maximum_age_ns) {}

  Status Bind(ChannelRef channel, const ChannelDescriptor& descriptor) noexcept {
    if (bound_ || route_id_ == 0 || descriptor.message_type.max_serialized_size == 0) {
      return bound_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    const auto status = channel.RegisterSubscriber(descriptor, Forward, this);
    if (IsOk(status)) {
      descriptor_ = descriptor;
      bound_ = true;
    }
    return status;
  }

  [[nodiscard]] const ChannelPacketEgressStats& stats() const noexcept { return stats_; }

 private:
  static constexpr std::uint64_t SaturatingAdd(std::uint64_t value,
                                               std::uint64_t increment) noexcept {
    return increment > UINT64_MAX - value ? UINT64_MAX : value + increment;
  }

  static Status Forward(void* state, std::span<const std::byte> message, const MessageInfo& info,
                        const ExecutionContext& caller) noexcept {
    return static_cast<ChannelPacketEgress*>(state)->Send(message, info, caller);
  }

  Status Send(std::span<const std::byte> message, const MessageInfo& info,
              const ExecutionContext& caller) noexcept {
    if (message.size() > descriptor_.message_type.max_serialized_size) {
      ++stats_.send_failures;
      return Status::kCapacityExceeded;
    }
    const auto deadline =
        maximum_age_ns_ == 0 ? 0 : SaturatingAdd(info.source_timestamp_ns, maximum_age_ns_);
    const PacketView packet{
        {route_id_, PacketKind::kChannel, info.sequence, info.source_timestamp_ns, deadline,
         descriptor_.message_type.schema_hash},
        message};
    const auto status = transport_.Send(packet, caller);
    if (!IsOk(status)) {
      ++stats_.send_failures;
      return status;
    }
    ++stats_.messages;
    return Status::kOk;
  }

  std::uint16_t route_id_{};
  Transport& transport_;
  std::uint64_t maximum_age_ns_{};
  ChannelDescriptor descriptor_{};
  bool bound_{};
  ChannelPacketEgressStats stats_{};
};

struct ChannelPacketIngressStats {
  std::uint32_t messages{};
  std::uint32_t expired{};
  std::uint32_t rejected{};
  std::uint32_t publish_failures{};
};

class ChannelPacketIngress {
 public:
  constexpr explicit ChannelPacketIngress(std::uint16_t route_id) noexcept : route_id_(route_id) {}

  template <std::size_t MaxRoutes>
  Status Bind(ChannelRef channel, StaticRouter<MaxRoutes>& router,
              const ChannelDescriptor& descriptor) noexcept {
    if (bound_ || route_id_ == 0 || descriptor.message_type.max_serialized_size == 0) {
      return bound_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    auto status = router.Register(route_id_, PacketKind::kChannel,
                                  descriptor.message_type.schema_hash, Receive, this);
    if (!IsOk(status)) {
      return status;
    }
    status = channel.RegisterPublisher(descriptor);
    if (!IsOk(status)) {
      return status;
    }
    channel_ = channel;
    descriptor_ = descriptor;
    bound_ = true;
    return Status::kOk;
  }

  [[nodiscard]] const ChannelPacketIngressStats& stats() const noexcept { return stats_; }

 private:
  static Status Receive(void* state, const PacketView& packet,
                        const ExecutionContext& caller) noexcept {
    return static_cast<ChannelPacketIngress*>(state)->Accept(packet, caller);
  }

  Status Accept(const PacketView& packet, const ExecutionContext& caller) noexcept {
    if (!bound_) {
      return Status::kInvalidState;
    }
    if (packet.header.route_id != route_id_ || packet.header.kind != PacketKind::kChannel ||
        packet.header.schema_hash != descriptor_.message_type.schema_hash ||
        packet.payload.size() > descriptor_.message_type.max_serialized_size) {
      ++stats_.rejected;
      return Status::kTypeMismatch;
    }
    if (packet.header.deadline_ns != 0 && caller.timestamp_ns() >= packet.header.deadline_ns) {
      ++stats_.expired;
      return Status::kTimeout;
    }
    const auto status =
        channel_.Publish(descriptor_, packet.payload, packet.header.source_timestamp_ns, caller);
    if (!IsOk(status)) {
      ++stats_.publish_failures;
      return status;
    }
    ++stats_.messages;
    return Status::kOk;
  }

  std::uint16_t route_id_{};
  ChannelRef channel_{};
  ChannelDescriptor descriptor_{};
  bool bound_{};
  ChannelPacketIngressStats stats_{};
};

}  // namespace aster::transport
