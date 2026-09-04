#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/channel.hpp"
#include "aster/status.hpp"
#include "aster/transport/can/control_plane.hpp"
#include "aster/transport/can/link.hpp"
#include "aster/transport/can/reliable_path.hpp"

namespace aster::transport::can {

struct ReliableChannelEgressStats {
  std::uint32_t messages{};
  std::uint32_t frames{};
  std::uint32_t acknowledgements{};
  std::uint32_t retries{};
  std::uint32_t timeouts{};
  std::uint32_t rate_limited{};
  std::uint32_t send_failures{};
};

template <std::size_t MaximumMessageSize>
class ReliableChannelEgress {
 public:
  static_assert(MaximumMessageSize > 0);
  static constexpr std::size_t kMaximumPayloadSize = MaximumMessageSize + 2U;
  static_assert(kMaximumPayloadSize <= 96, "reliable CAN Channel exceeds wire capacity");

  constexpr ReliableChannelEgress(std::uint16_t route_id, CanPriority priority,
                                  CanFrameWriter writer, CanTimeConverter time = {},
                                  std::uint64_t minimum_period_ns = 0,
                                  std::uint64_t retry_timeout_ns = 5'000'000,
                                  std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        writer_(writer),
        time_(time),
        minimum_period_ns_(minimum_period_ns),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status Bind(ChannelRef channel, const ChannelDescriptor& descriptor) noexcept {
    if (bound_ || descriptor.message_type.max_serialized_size == 0 ||
        descriptor.message_type.max_serialized_size > MaximumMessageSize ||
        retry_timeout_ns_ == 0) {
      return bound_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    const auto status = channel.RegisterSubscriber(descriptor, Forward, this);
    if (IsOk(status)) {
      descriptor_ = descriptor;
      bound_ = true;
    }
    return status;
  }

  Status AcceptAcknowledgement(const CanFrame& frame) noexcept {
    const auto before = sender_.stats().acknowledgements;
    const auto status = sender_.HandleAck(frame);
    if (sender_.stats().acknowledgements != before) {
      ++stats_.acknowledgements;
    }
    return status;
  }

  Status Poll(std::uint64_t now_ns, const ExecutionContext& caller) noexcept {
    const auto retries = sender_.stats().retries;
    Status status{Status::kUnavailable};
    if (sender_.state() == ReliableSenderState::kSending) {
      status = Pump(caller);
    } else if (sender_.state() == ReliableSenderState::kWaitingForAck) {
      status = sender_.Poll(now_ns);
      if (IsOk(status)) {
        status = Pump(caller);
      } else if (status == Status::kTimeout) {
        ++stats_.timeouts;
      }
    }
    stats_.retries += sender_.stats().retries - retries;
    return status;
  }

  void ResetPeer() noexcept { sender_ = {}; }

  [[nodiscard]] std::uint16_t route_id() const noexcept { return route_id_; }
  [[nodiscard]] const ReliableChannelEgressStats& stats() const noexcept { return stats_; }

 private:
  static bool SenderBusy(ReliableSenderState state) noexcept {
    return state == ReliableSenderState::kSending || state == ReliableSenderState::kWaitingForAck;
  }

  static Status Forward(void* state, std::span<const std::byte> message, const MessageInfo& info,
                        const ExecutionContext& caller) noexcept {
    return static_cast<ReliableChannelEgress*>(state)->Send(message, info, caller);
  }

  Status Send(std::span<const std::byte> message, const MessageInfo& info,
              const ExecutionContext& caller) noexcept {
    if (message.size() > descriptor_.message_type.max_serialized_size ||
        message.size() > MaximumMessageSize) {
      return Status::kCapacityExceeded;
    }
    if (SenderBusy(sender_.state())) {
      return Status::kCapacityExceeded;
    }
    const auto source_time_ns = time_.ToNetworkTime(info.source_timestamp_ns);
    if (has_last_send_ && source_time_ns >= last_send_time_ns_ &&
        source_time_ns - last_send_time_ns_ < minimum_period_ns_) {
      ++stats_.rate_limited;
      return Status::kOk;
    }
    const auto source_ms = source_time_ns / 1'000'000U;
    const auto tick = static_cast<std::uint16_t>(source_ms & 0xffffU);
    payload_[0] = static_cast<std::byte>(tick & 0xffU);
    payload_[1] = static_cast<std::byte>(tick >> 8U);
    std::copy(message.begin(), message.end(), payload_.begin() + 2);

    const auto payload = std::span<const std::byte>(payload_).first(message.size() + 2U);
    const auto status = sender_.Begin(route_id_, priority_, sequence_, payload, source_time_ns,
                                      retry_timeout_ns_, maximum_retries_);
    if (!IsOk(status)) {
      return status;
    }
    sequence_ = static_cast<std::uint8_t>((sequence_ + 1U) & 0x0fU);
    last_send_time_ns_ = source_time_ns;
    has_last_send_ = true;
    ++stats_.messages;
    return Pump(caller);
  }

  Status Pump(const ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (IsOk(sender_.NextFrame(frame))) {
      const auto status = writer_.Send(frame, caller);
      if (!IsOk(status)) {
        ++stats_.send_failures;
        return status;
      }
      ++stats_.frames;
    }
    return Status::kOk;
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  CanFrameWriter writer_;
  CanTimeConverter time_;
  std::uint64_t minimum_period_ns_{};
  std::uint64_t retry_timeout_ns_{};
  std::uint64_t last_send_time_ns_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t sequence_{};
  ChannelDescriptor descriptor_{};
  std::array<std::byte, kMaximumPayloadSize> payload_{};
  ReliableSender<kMaximumPayloadSize> sender_;
  bool has_last_send_{};
  bool bound_{};
  ReliableChannelEgressStats stats_{};
};

struct ReliableChannelIngressStats {
  std::uint32_t messages{};
  std::uint32_t acknowledgements{};
  std::uint32_t stale_messages{};
  std::uint32_t decode_failures{};
  std::uint32_t publish_failures{};
  std::uint32_t send_failures{};
  std::uint32_t reassembly_timeouts{};
};

template <std::size_t MaximumMessageSize>
class ReliableChannelIngress {
 public:
  static_assert(MaximumMessageSize > 0);
  static constexpr std::size_t kMaximumPayloadSize = MaximumMessageSize + 2U;
  static_assert(kMaximumPayloadSize <= 96, "reliable CAN Channel exceeds wire capacity");

  constexpr ReliableChannelIngress(std::uint16_t route_id, CanFrameWriter writer,
                                   FreshnessConfig freshness,
                                   std::uint64_t reassembly_timeout_ns = 100'000'000) noexcept
      : route_id_(route_id),
        writer_(writer),
        freshness_(freshness),
        receiver_(reassembly_timeout_ns) {}

  Status Bind(ChannelRef channel, const ChannelDescriptor& descriptor) noexcept {
    if (bound_ || descriptor.message_type.max_serialized_size == 0 ||
        descriptor.message_type.max_serialized_size > MaximumMessageSize) {
      return bound_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    const auto status = channel.RegisterPublisher(descriptor);
    if (IsOk(status)) {
      channel_ = channel;
      descriptor_ = descriptor;
      bound_ = true;
    }
    return status;
  }

  Status Accept(const CanFrame& frame, std::uint64_t receive_network_time_ns,
                const ExecutionContext& caller) noexcept {
    if (!bound_) {
      return Status::kInvalidState;
    }
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || id->route_id != route_id_ || frame.size == 0 ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable ||
        GetReliableSubtype(frame.data[0]) != ReliableSubtype::kData) {
      return Status::kInvalidArgument;
    }

    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto receive_status =
        receiver_.Accept(frame, receive_network_time_ns, message, acknowledgement);
    if (!IsOk(receive_status)) {
      if (acknowledgement.size != 0) {
        const auto ack_status = writer_.Send(acknowledgement, caller);
        if (!IsOk(ack_status)) {
          ++stats_.send_failures;
          return ack_status;
        }
        ++stats_.acknowledgements;
      }
      return receive_status;
    }
    if (message.payload.size() < 2U ||
        message.payload.size() - 2U > descriptor_.message_type.max_serialized_size) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }
    const auto tick = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(message.payload[0]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(message.payload[1])) << 8U));
    const auto source_timestamp_ns = ExpandTimestamp(tick, receive_network_time_ns);
    if (freshness_.Observe(message.sequence, source_timestamp_ns, receive_network_time_ns) !=
        FreshnessState::kFresh) {
      ++stats_.stale_messages;
      if (acknowledgement.size != 0) {
        static_cast<void>(writer_.Send(acknowledgement, caller));
      }
      return Status::kTimeout;
    }
    const auto publish_status =
        channel_.Publish(descriptor_, message.payload.subspan(2), source_timestamp_ns, caller);
    if (!IsOk(publish_status)) {
      ++stats_.publish_failures;
    }
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (!IsOk(ack_status)) {
        ++stats_.send_failures;
        return ack_status;
      }
      ++stats_.acknowledgements;
    }
    if (!IsOk(publish_status)) {
      return publish_status;
    }
    ++stats_.messages;
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns) noexcept {
    const auto status = receiver_.Poll(now_ns);
    if (status == Status::kTimeout) {
      ++stats_.reassembly_timeouts;
    }
    freshness_.Poll(now_ns);
    return status;
  }

  void ResetPeer() noexcept {
    receiver_.ResetPeerHistory();
    freshness_.Reset();
  }

  [[nodiscard]] std::uint16_t route_id() const noexcept { return route_id_; }
  [[nodiscard]] const ReliableChannelIngressStats& stats() const noexcept { return stats_; }

 private:
  static std::uint64_t ExpandTimestamp(std::uint16_t source_tick_ms,
                                       std::uint64_t receive_network_time_ns) noexcept {
    constexpr std::uint64_t kWrap = 65'536;
    constexpr std::uint64_t kHalfWrap = kWrap / 2U;
    const auto receive_ms = receive_network_time_ns / 1'000'000U;
    const auto base = receive_ms & ~(kWrap - 1U);
    auto candidate = base | source_tick_ms;
    if (candidate + kHalfWrap < receive_ms) {
      candidate += kWrap;
    } else if (candidate > receive_ms + kHalfWrap && candidate >= kWrap) {
      candidate -= kWrap;
    }
    return candidate * 1'000'000U;
  }

  std::uint16_t route_id_{};
  CanFrameWriter writer_;
  FreshnessGuard freshness_;
  ReliableReceiver<kMaximumPayloadSize> receiver_;
  ChannelRef channel_{};
  ChannelDescriptor descriptor_{};
  bool bound_{};
  ReliableChannelIngressStats stats_{};
};

}  // namespace aster::transport::can
