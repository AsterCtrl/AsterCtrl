#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/transport/can/protocol.hpp"

namespace aster::transport::can {

enum class ReliableSubtype : std::uint8_t {
  kData = 0,
  kAck = 1,
  kNack = 2,
  kCancel = 3,
};

constexpr std::byte ReliableHeader(ReliableSubtype subtype,
                                   std::uint8_t sequence) noexcept {
  const auto subtype_bits =
      static_cast<std::uint32_t>(static_cast<std::uint8_t>(subtype)) << 4U;
  const auto sequence_bits = static_cast<std::uint32_t>(sequence & 0x0fU);
  return static_cast<std::byte>(0x80U | subtype_bits | sequence_bits);
}

constexpr ReliableSubtype GetReliableSubtype(std::byte header) noexcept {
  return static_cast<ReliableSubtype>(
      (std::to_integer<std::uint8_t>(header) >> 4U) & 0x3U);
}

enum class ReliableSenderState : std::uint8_t {
  kIdle,
  kSending,
  kWaitingForAck,
  kComplete,
  kFailed,
};

struct ReliableSenderStats {
  std::uint32_t messages_started{};
  std::uint32_t frames_sent{};
  std::uint32_t retries{};
  std::uint32_t acknowledgements{};
  std::uint32_t failures{};
};

template <std::size_t MaximumPayload>
class ReliableSender {
 public:
  Status Begin(std::uint16_t route_id, CanPriority priority,
               std::uint8_t sequence, std::span<const std::byte> payload,
               std::uint64_t now_ns, std::uint64_t retry_timeout_ns,
               std::uint8_t maximum_retries) noexcept {
    if (state_ == ReliableSenderState::kSending ||
        state_ == ReliableSenderState::kWaitingForAck) {
      return Status::kInvalidState;
    }
    if (payload.empty() || payload.size() > MaximumPayload || sequence > 0x0fU ||
        retry_timeout_ns == 0) {
      return Status::kInvalidArgument;
    }
    if (const auto status =
            CanArbitrationId::Encode(priority, route_id, arbitration_id_);
        status != Status::kOk) {
      return status;
    }
    std::copy(payload.begin(), payload.end(), payload_.begin());
    payload_size_ = payload.size();
    sequence_ = sequence;
    next_fragment_ = 0;
    fragment_count_ = static_cast<std::uint8_t>((payload_size_ + 5U) / 6U);
    if (fragment_count_ > 16) {
      return Status::kCapacityExceeded;
    }
    retry_timeout_ns_ = retry_timeout_ns;
    retry_deadline_ns_ = now_ns + retry_timeout_ns;
    maximum_retries_ = maximum_retries;
    retry_count_ = 0;
    state_ = ReliableSenderState::kSending;
    ++stats_.messages_started;
    return Status::kOk;
  }

  Status NextFrame(CanFrame& frame) noexcept {
    if (state_ != ReliableSenderState::kSending) {
      return Status::kUnavailable;
    }
    const auto offset = static_cast<std::size_t>(next_fragment_) * 6U;
    const auto length = std::min<std::size_t>(6, payload_size_ - offset);
    frame = {};
    frame.arbitration_id = arbitration_id_;
    frame.size = static_cast<std::uint8_t>(length + 2U);
    frame.data[0] = ReliableHeader(ReliableSubtype::kData, sequence_);
    frame.data[1] = static_cast<std::byte>(
        ((fragment_count_ - 1U) << 4U) | next_fragment_);
    std::copy_n(payload_.begin() + static_cast<std::ptrdiff_t>(offset), length,
                frame.data.begin() + 2);
    ++next_fragment_;
    ++stats_.frames_sent;
    if (next_fragment_ == fragment_count_) {
      state_ = ReliableSenderState::kWaitingForAck;
    }
    return Status::kOk;
  }

  Status HandleAck(const CanFrame& frame) noexcept {
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || frame.size != 1 ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable ||
        GetReliableSubtype(frame.data[0]) != ReliableSubtype::kAck ||
        id->route_id != (arbitration_id_ & 0x1ffU) ||
        (std::to_integer<std::uint8_t>(frame.data[0]) & 0x0fU) != sequence_ ||
        (state_ != ReliableSenderState::kSending &&
         state_ != ReliableSenderState::kWaitingForAck)) {
      return Status::kInvalidArgument;
    }
    state_ = ReliableSenderState::kComplete;
    ++stats_.acknowledgements;
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns) noexcept {
    if (state_ != ReliableSenderState::kWaitingForAck ||
        now_ns < retry_deadline_ns_) {
      return Status::kUnavailable;
    }
    if (retry_count_ >= maximum_retries_) {
      state_ = ReliableSenderState::kFailed;
      ++stats_.failures;
      return Status::kTimeout;
    }
    ++retry_count_;
    ++stats_.retries;
    next_fragment_ = 0;
    retry_deadline_ns_ = now_ns + retry_timeout_ns_;
    state_ = ReliableSenderState::kSending;
    return Status::kOk;
  }

  ReliableSenderState state() const noexcept { return state_; }
  const ReliableSenderStats& stats() const noexcept { return stats_; }

 private:
  std::array<std::byte, MaximumPayload> payload_{};
  std::size_t payload_size_{};
  std::uint64_t retry_timeout_ns_{};
  std::uint64_t retry_deadline_ns_{};
  std::uint16_t arbitration_id_{};
  std::uint8_t sequence_{};
  std::uint8_t fragment_count_{};
  std::uint8_t next_fragment_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t retry_count_{};
  ReliableSenderState state_{ReliableSenderState::kIdle};
  ReliableSenderStats stats_{};
};

struct ReliableReceiverStats {
  std::uint32_t accepted_frames{};
  std::uint32_t completed{};
  std::uint32_t invalid{};
  std::uint32_t superseded{};
  std::uint32_t duplicate_fragments{};
  std::uint32_t duplicate_messages{};
};

template <std::size_t MaximumPayload>
class ReliableReceiver {
 public:
  Status Accept(const CanFrame& frame, ReassembledMessage& message,
                CanFrame& acknowledgement) noexcept {
    message = {};
    acknowledgement = {};
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || frame.size < 3 || frame.size > 8 ||
        GetFrameKind(frame.data[0]) != FrameKind::kReliable ||
        GetReliableSubtype(frame.data[0]) != ReliableSubtype::kData) {
      return Invalid();
    }
    const auto header = std::to_integer<std::uint8_t>(frame.data[0]);
    const auto sequence = static_cast<std::uint8_t>(header & 0x0fU);
    const auto fragment = std::to_integer<std::uint8_t>(frame.data[1]);
    const auto index = static_cast<std::uint8_t>(fragment & 0x0fU);
    const auto count = static_cast<std::uint8_t>((fragment >> 4U) + 1U);
    const auto length = static_cast<std::size_t>(frame.size - 2U);
    if (index >= count || count > 16 || length == 0 || length > 6 ||
        (index + 1U < count && length != 6) || count * 6U > MaximumPayload + 5U) {
      return Invalid();
    }
    if (!active_ || route_id_ != id->route_id || sequence_ != sequence) {
      if (active_) {
        ++stats_.superseded;
      }
      Reset();
      active_ = true;
      route_id_ = id->route_id;
      sequence_ = sequence;
      fragment_count_ = count;
    } else if (fragment_count_ != count) {
      Reset();
      return Invalid();
    }
    const auto bit = static_cast<std::uint16_t>(1U << index);
    if ((received_mask_ & bit) != 0) {
      ++stats_.duplicate_fragments;
      return Status::kUnavailable;
    }
    const auto offset = static_cast<std::size_t>(index) * 6U;
    if (offset + length > MaximumPayload) {
      Reset();
      return Invalid();
    }
    std::copy_n(frame.data.begin() + 2, static_cast<std::ptrdiff_t>(length),
                payload_.begin() + static_cast<std::ptrdiff_t>(offset));
    fragment_lengths_[index] = static_cast<std::uint8_t>(length);
    received_mask_ = static_cast<std::uint16_t>(received_mask_ | bit);
    ++stats_.accepted_frames;
    const auto expected = static_cast<std::uint16_t>((1U << count) - 1U);
    if (received_mask_ != expected) {
      return Status::kUnavailable;
    }
    std::size_t total{};
    for (std::size_t fragment_index = 0; fragment_index < count;
         ++fragment_index) {
      total += fragment_lengths_[fragment_index];
    }
    acknowledgement.arbitration_id = frame.arbitration_id;
    acknowledgement.size = 1;
    acknowledgement.data[0] = ReliableHeader(ReliableSubtype::kAck, sequence_);
    if (has_completed_ && last_completed_route_ == route_id_ &&
        last_completed_sequence_ == sequence_) {
      ++stats_.duplicate_messages;
      Reset();
      return Status::kUnavailable;
    }
    message = {route_id_, sequence_,
               std::span<const std::byte>(payload_.data(), total)};
    has_completed_ = true;
    last_completed_route_ = route_id_;
    last_completed_sequence_ = sequence_;
    ++stats_.completed;
    Reset();
    return Status::kOk;
  }

  const ReliableReceiverStats& stats() const noexcept { return stats_; }

 private:
  Status Invalid() noexcept {
    ++stats_.invalid;
    return Status::kInvalidArgument;
  }

  void Reset() noexcept {
    active_ = false;
    received_mask_ = 0;
    fragment_count_ = 0;
    fragment_lengths_.fill(0);
  }

  std::array<std::byte, MaximumPayload> payload_{};
  std::array<std::uint8_t, 16> fragment_lengths_{};
  std::uint16_t route_id_{};
  std::uint16_t last_completed_route_{};
  std::uint16_t received_mask_{};
  std::uint8_t sequence_{};
  std::uint8_t fragment_count_{};
  std::uint8_t last_completed_sequence_{};
  bool active_{};
  bool has_completed_{};
  ReliableReceiverStats stats_{};
};

}  // namespace aster::transport::can
