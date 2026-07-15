#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "xrobot/runtime/topic.hpp"
#include "xrobot/transport/can/control_plane.hpp"
#include "xrobot/transport/can/fast_path.hpp"
#include "xrobot/transport/can/link.hpp"

namespace xrobot::transport::can {

struct TopicEgressStats {
  std::uint32_t messages{};
  std::uint32_t frames{};
  std::uint32_t encode_failures{};
  std::uint32_t send_failures{};
};

template <xrobot::runtime::MessageType Message>
class FastTopicEgress final : public xrobot::runtime::TopicSink<Message> {
 public:
  static constexpr std::size_t kMessageSize =
      xrobot::runtime::TypeSupport<Message>::descriptor().max_serialized_size;
  static constexpr std::size_t kPayloadSize = kMessageSize + 2;
  static constexpr std::size_t kMaximumFrames =
      kPayloadSize <= 7 ? 1 : (kPayloadSize + 5) / 6;

  constexpr FastTopicEgress(std::uint16_t route_id, CanPriority priority,
                            CanFrameWriter writer) noexcept
      : route_id_(route_id), priority_(priority), writer_(writer) {}

  Status Deliver(
      const Message& message, const xrobot::runtime::MessageInfo& info,
      const xrobot::runtime::ExecutionContext& caller) noexcept override {
    const auto source_ms = info.source_timestamp_ns / 1'000'000U;
    const auto tick = static_cast<std::uint16_t>(source_ms & 0xffffU);
    payload_[0] = static_cast<std::byte>(tick & 0xffU);
    payload_[1] = static_cast<std::byte>(tick >> 8U);
    std::size_t written{};
    const auto encode_status = xrobot::runtime::TypeSupport<Message>::Encode(
        message, std::span<std::byte>(payload_).subspan(2), written);
    if (encode_status != Status::kOk || written != kMessageSize) {
      ++stats_.encode_failures;
      return encode_status == Status::kOk ? Status::kInternal : encode_status;
    }

    std::size_t frame_count{};
    const auto frame_status = FastCodec::Encode(
        route_id_, priority_, sequence_, payload_, frames_, frame_count);
    if (frame_status != Status::kOk) {
      ++stats_.encode_failures;
      return frame_status;
    }
    for (std::size_t index = 0; index < frame_count; ++index) {
      const auto status = writer_.Send(frames_[index], caller);
      if (status != Status::kOk) {
        ++stats_.send_failures;
        return status;
      }
      ++stats_.frames;
    }
    sequence_ = static_cast<std::uint8_t>((sequence_ + 1U) & 0x3fU);
    ++stats_.messages;
    return Status::kOk;
  }

  const TopicEgressStats& stats() const noexcept { return stats_; }

 private:
  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  CanFrameWriter writer_;
  std::array<std::byte, kPayloadSize> payload_{};
  std::array<CanFrame, kMaximumFrames> frames_{};
  std::uint8_t sequence_{};
  TopicEgressStats stats_{};
};

struct TopicIngressStats {
  std::uint32_t messages{};
  std::uint32_t stale_messages{};
  std::uint32_t decode_failures{};
  std::uint32_t publish_failures{};
};

template <xrobot::runtime::MessageType Message>
class FastTopicIngress {
 public:
  static constexpr std::size_t kMessageSize =
      xrobot::runtime::TypeSupport<Message>::descriptor().max_serialized_size;
  static constexpr std::size_t kPayloadSize = kMessageSize + 2;

  constexpr FastTopicIngress(
      std::uint16_t route_id, xrobot::runtime::TopicPublisher<Message> publisher,
      FreshnessConfig freshness) noexcept
      : route_id_(route_id), publisher_(publisher), freshness_(freshness) {}

  Status Accept(const CanFrame& frame, std::uint64_t receive_network_time_ns,
                const xrobot::runtime::ExecutionContext& caller) noexcept {
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value()) {
      return Status::kInvalidArgument;
    }
    if (id->route_id != route_id_) {
      return Status::kUnavailable;
    }
    ReassembledMessage reassembled;
    const auto status = reassembler_.Accept(frame, reassembled);
    if (status != Status::kOk) {
      return status;
    }
    if (reassembled.payload.size() != kPayloadSize) {
      ++stats_.decode_failures;
      return Status::kTypeMismatch;
    }
    const auto tick = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(reassembled.payload[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(reassembled.payload[1]) << 8U));
    const auto source_timestamp_ns =
        ExpandTimestamp(tick, receive_network_time_ns);
    if (freshness_.Observe(reassembled.sequence, source_timestamp_ns,
                           receive_network_time_ns) != FreshnessState::kFresh) {
      ++stats_.stale_messages;
      return Status::kTimeout;
    }
    Message message;
    const auto decode_status = xrobot::runtime::TypeSupport<Message>::Decode(
        reassembled.payload.subspan(2), message);
    if (decode_status != Status::kOk) {
      ++stats_.decode_failures;
      return decode_status;
    }
    const auto publish_status =
        publisher_.Publish(message, source_timestamp_ns, caller);
    if (publish_status != Status::kOk) {
      ++stats_.publish_failures;
      return publish_status;
    }
    ++stats_.messages;
    return Status::kOk;
  }

  FreshnessGuard& freshness() noexcept { return freshness_; }
  const FastReassemblyStats& reassembly_stats() const noexcept {
    return reassembler_.stats();
  }
  const TopicIngressStats& stats() const noexcept { return stats_; }

 private:
  static std::uint64_t ExpandTimestamp(
      std::uint16_t source_tick_ms,
      std::uint64_t receive_network_time_ns) noexcept {
    constexpr std::uint64_t kWrap = 65'536;
    constexpr std::uint64_t kHalfWrap = kWrap / 2;
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
  xrobot::runtime::TopicPublisher<Message> publisher_;
  FastReassembler<kPayloadSize> reassembler_;
  FreshnessGuard freshness_;
  TopicIngressStats stats_{};
};

}  // namespace xrobot::transport::can
