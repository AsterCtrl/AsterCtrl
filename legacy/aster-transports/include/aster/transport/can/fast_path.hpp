#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/transport/can/protocol.hpp"

namespace aster::transport::can {

class FastCodec {
 public:
  static Status Encode(std::uint16_t route_id, CanPriority priority,
                       std::uint8_t sequence,
                       std::span<const std::byte> payload,
                       std::span<CanFrame> output,
                       std::size_t& frame_count) noexcept {
    frame_count = 0;
    if (sequence > 0x3fU || payload.empty()) {
      return Status::kInvalidArgument;
    }
    std::uint16_t arbitration_id{};
    if (const auto status =
            CanArbitrationId::Encode(priority, route_id, arbitration_id);
        status != Status::kOk) {
      return status;
    }

    if (payload.size() <= 7) {
      if (output.empty()) {
        return Status::kCapacityExceeded;
      }
      auto& frame = output[0];
      frame = {};
      frame.arbitration_id = arbitration_id;
      frame.size = static_cast<std::uint8_t>(payload.size() + 1);
      frame.data[0] = static_cast<std::byte>(sequence);
      std::copy(payload.begin(), payload.end(), frame.data.begin() + 1);
      frame_count = 1;
      return Status::kOk;
    }

    constexpr std::size_t kFragmentPayload = 6;
    const auto count = (payload.size() + kFragmentPayload - 1) / kFragmentPayload;
    if (count > 16 || output.size() < count) {
      return Status::kCapacityExceeded;
    }
    for (std::size_t index = 0; index < count; ++index) {
      const auto offset = index * kFragmentPayload;
      const auto length = std::min(kFragmentPayload, payload.size() - offset);
      auto& frame = output[index];
      frame = {};
      frame.arbitration_id = arbitration_id;
      frame.size = static_cast<std::uint8_t>(length + 2);
      frame.data[0] = static_cast<std::byte>(0x40U | sequence);
      frame.data[1] = static_cast<std::byte>(
          ((static_cast<std::uint8_t>(count) - 1U) << 4U) |
          static_cast<std::uint8_t>(index));
      std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(offset), length,
                  frame.data.begin() + 2);
    }
    frame_count = count;
    return Status::kOk;
  }
};

struct FastReassemblyStats {
  std::uint32_t accepted_frames{};
  std::uint32_t completed{};
  std::uint32_t invalid{};
  std::uint32_t duplicate_fragments{};
  std::uint32_t replayed_frames{};
  std::uint32_t stale_frames{};
  std::uint32_t superseded{};
  std::uint32_t sequence_gaps{};
};

template <std::size_t MaximumPayload>
class FastReassembler {
 public:
  static_assert(MaximumPayload > 0);

  Status Accept(const CanFrame& frame, ReassembledMessage& message) noexcept {
    message = {};
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || frame.size == 0 || frame.size > 8) {
      return Invalid();
    }
    const auto kind = GetFrameKind(frame.data[0]);
    const auto header = std::to_integer<std::uint8_t>(frame.data[0]);
    const auto sequence = static_cast<std::uint8_t>(header & 0x3fU);
    if (kind == FrameKind::kFastSingle) {
      const auto length = static_cast<std::size_t>(frame.size - 1U);
      if (length > MaximumPayload) {
        return Invalid();
      }
      const auto sequence_status = PrepareSequence(id->route_id, sequence, false);
      if (sequence_status != Status::kOk) {
        return sequence_status;
      }
      std::copy_n(frame.data.begin() + 1,
                  static_cast<std::ptrdiff_t>(length), buffer_.begin());
      Complete(id->route_id, sequence, length, message);
      ++stats_.accepted_frames;
      return Status::kOk;
    }
    if (kind != FrameKind::kFastFragment || frame.size < 3) {
      return Invalid();
    }

    const auto fragment = std::to_integer<std::uint8_t>(frame.data[1]);
    const auto index = static_cast<std::uint8_t>(fragment & 0x0fU);
    const auto count = static_cast<std::uint8_t>((fragment >> 4U) + 1U);
    const auto length = static_cast<std::size_t>(frame.size - 2U);
    if (index >= count || count > 16 || length == 0 || length > 6 ||
        (index + 1U < count && length != 6) || count * 6U > MaximumPayload + 5U) {
      return Invalid();
    }

    if (!active_ || route_id_ != id->route_id || sequence_ != sequence) {
      const auto sequence_status = PrepareSequence(id->route_id, sequence, true);
      if (sequence_status != Status::kOk) {
        return sequence_status;
      }
      active_ = true;
      route_id_ = id->route_id;
      sequence_ = sequence;
      fragment_count_ = count;
      received_mask_ = 0;
      fragment_lengths_.fill(0);
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
                buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    fragment_lengths_[index] = static_cast<std::uint8_t>(length);
    received_mask_ = static_cast<std::uint16_t>(received_mask_ | bit);
    ++stats_.accepted_frames;

    const auto expected_mask = static_cast<std::uint16_t>((1U << count) - 1U);
    if (received_mask_ != expected_mask) {
      return Status::kUnavailable;
    }
    std::size_t total{};
    for (std::size_t fragment_index = 0; fragment_index < count;
         ++fragment_index) {
      total += fragment_lengths_[fragment_index];
    }
    Complete(route_id_, sequence_, total, message);
    Reset();
    return Status::kOk;
  }

  const FastReassemblyStats& stats() const noexcept { return stats_; }

  void ResetHistory() noexcept {
    Reset();
    has_last_sequence_ = false;
    last_route_id_ = 0;
    last_sequence_ = 0;
  }

 private:
  Status Invalid() noexcept {
    ++stats_.invalid;
    return Status::kInvalidArgument;
  }

  Status PrepareSequence(std::uint16_t route_id, std::uint8_t sequence,
                         bool continuing_fragment_allowed) noexcept {
    if (active_) {
      if (route_id != route_id_) {
        SupersedeActive();
      } else if (sequence == sequence_) {
        if (continuing_fragment_allowed) {
          return Status::kOk;
        }
        ++stats_.replayed_frames;
        return Status::kUnavailable;
      } else {
        const auto active_distance = static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(sequence - sequence_)) & 0x3fU);
        if (active_distance > 31U) {
          ++stats_.stale_frames;
          return Status::kUnavailable;
        }
        SupersedeActive();
      }
    }
    if (!has_last_sequence_ || last_route_id_ != route_id) {
      return Status::kOk;
    }
    const auto completed_distance = static_cast<std::uint8_t>(
        (static_cast<std::uint8_t>(sequence - last_sequence_)) & 0x3fU);
    if (completed_distance == 0U) {
      ++stats_.replayed_frames;
      return Status::kUnavailable;
    }
    if (completed_distance > 31U) {
      ++stats_.stale_frames;
      return Status::kUnavailable;
    }
    return Status::kOk;
  }

  void SupersedeActive() noexcept {
    if (active_) {
      ++stats_.superseded;
    }
    Reset();
  }

  void Complete(std::uint16_t route_id, std::uint8_t sequence,
                std::size_t size, ReassembledMessage& message) noexcept {
    if (has_last_sequence_ && last_route_id_ == route_id) {
      const auto expected = static_cast<std::uint8_t>((last_sequence_ + 1U) & 0x3fU);
      if (sequence != expected) {
        ++stats_.sequence_gaps;
      }
    }
    has_last_sequence_ = true;
    last_route_id_ = route_id;
    last_sequence_ = sequence;
    message = {route_id, sequence,
               std::span<const std::byte>(buffer_.data(), size)};
    ++stats_.completed;
  }

  void Reset() noexcept {
    active_ = false;
    received_mask_ = 0;
    fragment_count_ = 0;
  }

  std::array<std::byte, MaximumPayload> buffer_{};
  std::array<std::uint8_t, 16> fragment_lengths_{};
  std::uint16_t route_id_{};
  std::uint16_t last_route_id_{};
  std::uint16_t received_mask_{};
  std::uint8_t sequence_{};
  std::uint8_t fragment_count_{};
  std::uint8_t last_sequence_{};
  bool active_{};
  bool has_last_sequence_{};
  FastReassemblyStats stats_{};
};

}  // namespace aster::transport::can
