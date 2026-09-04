#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/channel.hpp"
#include "aster/status.hpp"
#include "aster/transport/can/control_plane.hpp"
#include "aster/transport/can/fast_path.hpp"
#include "aster/transport/can/link.hpp"

namespace aster::transport::can {

struct ChannelEgressStats {
  std::uint32_t messages{};
  std::uint32_t frames{};
  std::uint32_t rate_limited{};
  std::uint32_t send_failures{};
};

template <std::size_t MaximumMessageSize>
class FastChannelEgress {
 public:
  static_assert(MaximumMessageSize > 0);
  static constexpr std::size_t kMaximumPayloadSize = MaximumMessageSize + 2U;
  static constexpr std::size_t kMaximumFrames =
      kMaximumPayloadSize <= 7U ? 1U : (kMaximumPayloadSize + 5U) / 6U;

  constexpr FastChannelEgress(std::uint16_t route_id, CanPriority priority, CanFrameWriter writer,
                              CanTimeConverter time = {},
                              std::uint64_t minimum_period_ns = 0) noexcept
      : route_id_(route_id),
        priority_(priority),
        writer_(writer),
        time_(time),
        minimum_period_ns_(minimum_period_ns) {}

  Status Bind(ChannelRef channel, const ChannelDescriptor& descriptor) noexcept {
    if (bound_ || descriptor.message_type.max_serialized_size == 0 ||
        descriptor.message_type.max_serialized_size > MaximumMessageSize) {
      return bound_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    const auto status = channel.RegisterSubscriber(descriptor, Forward, this);
    if (IsOk(status)) {
      descriptor_ = descriptor;
      bound_ = true;
    }
    return status;
  }

  [[nodiscard]] const ChannelEgressStats& stats() const noexcept { return stats_; }

 private:
  static Status Forward(void* state, std::span<const std::byte> message, const MessageInfo& info,
                        const ExecutionContext& caller) noexcept {
    return static_cast<FastChannelEgress*>(state)->Send(message, info, caller);
  }

  Status Send(std::span<const std::byte> message, const MessageInfo& info,
              const ExecutionContext& caller) noexcept {
    if (message.size() > descriptor_.message_type.max_serialized_size ||
        message.size() > MaximumMessageSize) {
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

    std::size_t frame_count{};
    auto status = FastCodec::Encode(route_id_, priority_, sequence_,
                                    std::span<const std::byte>(payload_).first(message.size() + 2U),
                                    frames_, frame_count);
    if (!IsOk(status)) {
      return status;
    }
    for (std::size_t index = 0; index < frame_count; ++index) {
      status = writer_.Send(frames_[index], caller);
      if (!IsOk(status)) {
        ++stats_.send_failures;
        return status;
      }
      ++stats_.frames;
    }
    sequence_ = static_cast<std::uint8_t>((sequence_ + 1U) & 0x3fU);
    last_send_time_ns_ = source_time_ns;
    has_last_send_ = true;
    ++stats_.messages;
    return Status::kOk;
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  CanFrameWriter writer_;
  CanTimeConverter time_;
  std::uint64_t minimum_period_ns_{};
  std::uint64_t last_send_time_ns_{};
  ChannelDescriptor descriptor_{};
  std::array<std::byte, kMaximumPayloadSize> payload_{};
  std::array<CanFrame, kMaximumFrames> frames_{};
  std::uint8_t sequence_{};
  bool has_last_send_{};
  bool bound_{};
  ChannelEgressStats stats_{};
};

struct ChannelIngressStats {
  std::uint32_t messages{};
  std::uint32_t stale_messages{};
  std::uint32_t decode_failures{};
  std::uint32_t publish_failures{};
};

template <std::size_t MaximumMessageSize>
class FastChannelIngress {
 public:
  static_assert(MaximumMessageSize > 0);
  static constexpr std::size_t kMaximumPayloadSize = MaximumMessageSize + 2U;

  constexpr FastChannelIngress(std::uint16_t route_id, FreshnessConfig freshness) noexcept
      : route_id_(route_id), freshness_(freshness) {}

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
    if (!id.has_value()) {
      return Status::kInvalidArgument;
    }
    if (id->route_id != route_id_) {
      return Status::kUnavailable;
    }
    ReassembledMessage reassembled;
    const auto status = reassembler_.Accept(frame, reassembled);
    if (!IsOk(status)) {
      return status;
    }
    if (reassembled.payload.size() < 2U ||
        reassembled.payload.size() - 2U > descriptor_.message_type.max_serialized_size) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }
    const auto tick = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(reassembled.payload[0]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(reassembled.payload[1])) << 8U));
    const auto source_timestamp_ns = ExpandTimestamp(tick, receive_network_time_ns);
    if (freshness_.Observe(reassembled.sequence, source_timestamp_ns, receive_network_time_ns) !=
        FreshnessState::kFresh) {
      ++stats_.stale_messages;
      return Status::kTimeout;
    }
    const auto publish_status =
        channel_.Publish(descriptor_, reassembled.payload.subspan(2), source_timestamp_ns, caller);
    if (!IsOk(publish_status)) {
      ++stats_.publish_failures;
      return publish_status;
    }
    ++stats_.messages;
    return Status::kOk;
  }

  [[nodiscard]] FreshnessGuard& freshness() noexcept { return freshness_; }
  void ResetPeer() noexcept {
    reassembler_.ResetHistory();
    freshness_.Reset();
  }
  [[nodiscard]] const FastReassemblyStats& reassembly_stats() const noexcept {
    return reassembler_.stats();
  }
  [[nodiscard]] const ChannelIngressStats& stats() const noexcept { return stats_; }

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
  ChannelRef channel_{};
  ChannelDescriptor descriptor_{};
  FastReassembler<kMaximumPayloadSize> reassembler_{};
  FreshnessGuard freshness_;
  bool bound_{};
  ChannelIngressStats stats_{};
};

}  // namespace aster::transport::can
