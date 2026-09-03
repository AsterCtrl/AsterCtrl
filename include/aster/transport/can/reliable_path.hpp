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

constexpr std::byte ReliableHeader(ReliableSubtype subtype, std::uint8_t sequence) noexcept {
  const auto subtype_bits = static_cast<std::uint32_t>(static_cast<std::uint8_t>(subtype)) << 4U;
  const auto sequence_bits = static_cast<std::uint32_t>(sequence & 0x0fU);
  return static_cast<std::byte>(0x80U | subtype_bits | sequence_bits);
}

constexpr ReliableSubtype GetReliableSubtype(std::byte header) noexcept {
  return static_cast<ReliableSubtype>((std::to_integer<std::uint8_t>(header) >> 4U) & 0x3U);
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
  std::uint32_t duplicate_acknowledgements{};
  std::uint32_t failures{};
  std::uint32_t write_rejections{};
};

template <std::size_t MaximumPayload>
class ReliableSender {
 public:
  static_assert(MaximumPayload > 0);

  Status Begin(std::uint16_t route_id, CanPriority priority, std::uint8_t sequence,
               std::span<const std::byte> payload, std::uint64_t now_ns,
               std::uint64_t retry_timeout_ns, std::uint8_t maximum_retries) noexcept {
    if (state_ == ReliableSenderState::kSending || state_ == ReliableSenderState::kWaitingForAck) {
      return Status::kInvalidState;
    }
    if (payload.empty() || payload.size() > MaximumPayload || sequence > 0x0fU ||
        retry_timeout_ns == 0) {
      return Status::kInvalidArgument;
    }
    if (const auto status = CanArbitrationId::Encode(priority, route_id, arbitration_id_);
        status != Status::kOk) {
      return status;
    }
    const auto fragment_count = (payload.size() + 5U) / 6U;
    if (fragment_count > 16) {
      return Status::kCapacityExceeded;
    }
    std::copy(payload.begin(), payload.end(), payload_.begin());
    payload_size_ = payload.size();
    sequence_ = sequence;
    next_fragment_ = 0;
    fragment_count_ = static_cast<std::uint8_t>(fragment_count);
    retry_timeout_ns_ = retry_timeout_ns;
    retry_deadline_ns_ = SaturatingAdd(now_ns, retry_timeout_ns);
    maximum_retries_ = maximum_retries;
    retry_count_ = 0;
    pending_write_ = false;
    state_ = ReliableSenderState::kSending;
    ++stats_.messages_started;
    return Status::kOk;
  }

  Status NextFrame(CanFrame& frame) noexcept {
    if (state_ != ReliableSenderState::kSending) {
      return Status::kUnavailable;
    }
    const auto fragment = next_fragment_;
    const auto offset = static_cast<std::size_t>(fragment) * 6U;
    const auto length = std::min<std::size_t>(6, payload_size_ - offset);
    frame = {};
    frame.arbitration_id = arbitration_id_;
    frame.size = static_cast<std::uint8_t>(length + 2U);
    frame.data[0] = ReliableHeader(ReliableSubtype::kData, sequence_);
    frame.data[1] = static_cast<std::byte>(((fragment_count_ - 1U) << 4U) | fragment);
    std::copy_n(payload_.begin() + static_cast<std::ptrdiff_t>(offset), length,
                frame.data.begin() + 2);
    next_fragment_ = static_cast<std::uint8_t>(fragment + 1U);
    next_write_token_ = next_write_token_ == UINT32_MAX ? 1 : next_write_token_ + 1U;
    pending_write_token_ = next_write_token_;
    pending_fragment_ = fragment;
    pending_write_ = true;
    frame.write_completion = CompleteFrameWrite;
    frame.write_state = this;
    frame.write_token = pending_write_token_;
    ++stats_.frames_sent;
    if (next_fragment_ == fragment_count_) {
      state_ = ReliableSenderState::kWaitingForAck;
    }
    return Status::kOk;
  }

  Status HandleAck(const CanFrame& frame) noexcept {
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || frame.size != 1 || GetFrameKind(frame.data[0]) != FrameKind::kReliable ||
        GetReliableSubtype(frame.data[0]) != ReliableSubtype::kAck ||
        frame.arbitration_id != arbitration_id_ ||
        (std::to_integer<std::uint8_t>(frame.data[0]) & 0x0fU) != sequence_) {
      return Status::kInvalidArgument;
    }
    if (state_ == ReliableSenderState::kComplete) {
      ++stats_.duplicate_acknowledgements;
      return Status::kUnavailable;
    }
    if (state_ != ReliableSenderState::kWaitingForAck) return Status::kInvalidArgument;
    state_ = ReliableSenderState::kComplete;
    pending_write_ = false;
    ++stats_.acknowledgements;
    return Status::kOk;
  }

  Status Poll(std::uint64_t now_ns) noexcept {
    if (state_ != ReliableSenderState::kWaitingForAck || now_ns < retry_deadline_ns_) {
      return Status::kUnavailable;
    }
    if (retry_count_ >= maximum_retries_) {
      state_ = ReliableSenderState::kFailed;
      pending_write_ = false;
      ++stats_.failures;
      return Status::kTimeout;
    }
    ++retry_count_;
    ++stats_.retries;
    next_fragment_ = 0;
    pending_write_ = false;
    retry_deadline_ns_ = SaturatingAdd(now_ns, retry_timeout_ns_);
    state_ = ReliableSenderState::kSending;
    return Status::kOk;
  }

  Status ArmAckDeadline(std::uint64_t now_ns) noexcept {
    if (state_ != ReliableSenderState::kWaitingForAck) {
      return Status::kInvalidState;
    }
    retry_deadline_ns_ = SaturatingAdd(now_ns, retry_timeout_ns_);
    return Status::kOk;
  }

  ReliableSenderState state() const noexcept { return state_; }
  const ReliableSenderStats& stats() const noexcept { return stats_; }

 private:
  static constexpr std::uint64_t SaturatingAdd(std::uint64_t value,
                                               std::uint64_t increment) noexcept {
    return increment > UINT64_MAX - value ? UINT64_MAX : value + increment;
  }

  static void CompleteFrameWrite(void* state, std::uint32_t token, Status status) noexcept {
    static_cast<ReliableSender*>(state)->OnFrameWrite(token, status);
  }

  void OnFrameWrite(std::uint32_t token, Status status) noexcept {
    if (!pending_write_ || token != pending_write_token_) {
      return;
    }
    pending_write_ = false;
    if (status == Status::kOk) {
      return;
    }
    ++stats_.write_rejections;
    const auto expected_next = static_cast<std::uint8_t>(pending_fragment_ + 1U);
    if (next_fragment_ == expected_next) {
      next_fragment_ = pending_fragment_;
      state_ = ReliableSenderState::kSending;
    }
  }

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
  std::uint8_t pending_fragment_{};
  std::uint32_t next_write_token_{};
  std::uint32_t pending_write_token_{};
  bool pending_write_{};
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
  std::uint32_t stale_messages{};
  std::uint32_t sequence_gaps{};
  std::uint32_t reassembly_timeouts{};
};

template <std::size_t MaximumPayload>
class ReliableReceiver {
 public:
  static_assert(MaximumPayload > 0);

  constexpr explicit ReliableReceiver(std::uint64_t reassembly_timeout_ns = 0) noexcept
      : reassembly_timeout_ns_(reassembly_timeout_ns) {}

  Status Accept(const CanFrame& frame, ReassembledMessage& message,
                CanFrame& acknowledgement) noexcept {
    return AcceptImpl(frame, 0, false, message, acknowledgement);
  }

  Status Accept(const CanFrame& frame, std::uint64_t receive_time_ns, ReassembledMessage& message,
                CanFrame& acknowledgement) noexcept {
    if (has_observed_time_ && receive_time_ns < last_observed_time_ns_) {
      message = {};
      acknowledgement = {};
      return Invalid();
    }
    has_observed_time_ = true;
    last_observed_time_ns_ = receive_time_ns;
    ExpireIfDue(receive_time_ns);
    return AcceptImpl(frame, receive_time_ns, true, message, acknowledgement);
  }

  Status Poll(std::uint64_t now_ns) noexcept {
    if (has_observed_time_ && now_ns < last_observed_time_ns_) {
      return Status::kInvalidArgument;
    }
    has_observed_time_ = true;
    last_observed_time_ns_ = now_ns;
    return ExpireIfDue(now_ns) ? Status::kTimeout : Status::kUnavailable;
  }

  void ResetPeerHistory() noexcept {
    Reset();
    has_completed_ = false;
    last_completed_arbitration_id_ = 0;
    last_completed_sequence_ = 0;
    has_observed_time_ = false;
    last_observed_time_ns_ = 0;
  }

  const ReliableReceiverStats& stats() const noexcept { return stats_; }

 private:
  Status AcceptImpl(const CanFrame& frame, std::uint64_t receive_time_ns, bool timed,
                    ReassembledMessage& message, CanFrame& acknowledgement) noexcept {
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
    const auto offset = static_cast<std::size_t>(index) * 6U;
    if (index >= count || count > 16 || length == 0 || length > 6 ||
        (index + 1U < count && length != 6) || count * 6U > MaximumPayload + 5U) {
      return Invalid();
    }
    if (offset + length > MaximumPayload) {
      return Invalid();
    }
    if (!active_ || arbitration_id_ != frame.arbitration_id || sequence_ != sequence) {
      if (const auto status = PrepareMessage(frame.arbitration_id, sequence);
          status != Status::kOk) {
        return status;
      }
      active_ = true;
      arbitration_id_ = frame.arbitration_id;
      route_id_ = id->route_id;
      sequence_ = sequence;
      fragment_count_ = count;
      if (timed && reassembly_timeout_ns_ != 0) {
        reassembly_deadline_ns_ = SaturatingAdd(receive_time_ns, reassembly_timeout_ns_);
      }
    } else if (fragment_count_ != count) {
      return Invalid();
    }
    const auto bit = static_cast<std::uint16_t>(1U << index);
    if ((received_mask_ & bit) != 0) {
      ++stats_.duplicate_fragments;
      return Status::kUnavailable;
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
    for (std::size_t fragment_index = 0; fragment_index < count; ++fragment_index) {
      total += fragment_lengths_[fragment_index];
    }
    acknowledgement.arbitration_id = frame.arbitration_id;
    acknowledgement.size = 1;
    acknowledgement.data[0] = ReliableHeader(ReliableSubtype::kAck, sequence_);
    if (has_completed_ && last_completed_arbitration_id_ == arbitration_id_ &&
        last_completed_sequence_ == sequence_) {
      ++stats_.duplicate_messages;
      Reset();
      return Status::kUnavailable;
    }
    message = {route_id_, sequence_, std::span<const std::byte>(payload_.data(), total)};
    has_completed_ = true;
    last_completed_arbitration_id_ = arbitration_id_;
    last_completed_sequence_ = sequence_;
    ++stats_.completed;
    Reset();
    return Status::kOk;
  }

  static constexpr std::uint64_t SaturatingAdd(std::uint64_t value,
                                               std::uint64_t increment) noexcept {
    return increment > UINT64_MAX - value ? UINT64_MAX : value + increment;
  }

  Status PrepareMessage(std::uint16_t arbitration_id, std::uint8_t sequence) noexcept {
    if (active_) {
      if (arbitration_id_ != arbitration_id) {
        ++stats_.superseded;
        Reset();
      } else {
        const auto distance = SequenceDistance(sequence_, sequence);
        if (distance == 0U) {
          return Status::kOk;
        }
        if (distance > 7U) {
          ++stats_.stale_messages;
          return Status::kUnavailable;
        }
        ++stats_.superseded;
        Reset();
      }
    }
    if (!has_completed_ || last_completed_arbitration_id_ != arbitration_id) {
      return Status::kOk;
    }
    const auto distance = SequenceDistance(last_completed_sequence_, sequence);
    if (distance == 0U) {
      return Status::kOk;
    }
    if (distance > 7U) {
      ++stats_.stale_messages;
      return Status::kUnavailable;
    }
    if (distance > 1U) {
      ++stats_.sequence_gaps;
    }
    return Status::kOk;
  }

  static constexpr std::uint8_t SequenceDistance(std::uint8_t from, std::uint8_t to) noexcept {
    return static_cast<std::uint8_t>((to - from) & 0x0fU);
  }

  bool ExpireIfDue(std::uint64_t now_ns) noexcept {
    if (!active_ || reassembly_timeout_ns_ == 0 || now_ns < reassembly_deadline_ns_) {
      return false;
    }
    Reset();
    ++stats_.reassembly_timeouts;
    return true;
  }

  Status Invalid() noexcept {
    ++stats_.invalid;
    return Status::kInvalidArgument;
  }

  void Reset() noexcept {
    active_ = false;
    received_mask_ = 0;
    fragment_count_ = 0;
    reassembly_deadline_ns_ = 0;
    fragment_lengths_.fill(0);
  }

  std::array<std::byte, MaximumPayload> payload_{};
  std::array<std::uint8_t, 16> fragment_lengths_{};
  std::uint16_t route_id_{};
  std::uint16_t arbitration_id_{};
  std::uint16_t last_completed_arbitration_id_{};
  std::uint16_t received_mask_{};
  std::uint64_t reassembly_timeout_ns_{};
  std::uint64_t reassembly_deadline_ns_{};
  std::uint64_t last_observed_time_ns_{};
  std::uint8_t sequence_{};
  std::uint8_t fragment_count_{};
  std::uint8_t last_completed_sequence_{};
  bool active_{};
  bool has_completed_{};
  bool has_observed_time_{};
  ReliableReceiverStats stats_{};
};

}  // namespace aster::transport::can
