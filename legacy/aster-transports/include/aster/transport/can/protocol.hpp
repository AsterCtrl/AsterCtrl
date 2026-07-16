#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "aster/runtime/status.hpp"

namespace aster::transport::can {

using aster::runtime::Status;

inline constexpr std::uint16_t kMaximumRouteId = 511;
inline constexpr std::uint16_t kFirstApplicationRouteId = 8;
inline constexpr std::size_t kClassicCanPayloadSize = 8;

enum class CanPriority : std::uint8_t {
  kCritical = 0,
  kControl = 1,
  kState = 2,
  kBackground = 3,
};

struct DecodedArbitrationId {
  CanPriority priority{CanPriority::kBackground};
  std::uint16_t route_id{};
};

class CanArbitrationId {
 public:
  static constexpr Status Encode(CanPriority priority, std::uint16_t route_id,
                                 std::uint16_t& encoded) noexcept {
    if (route_id == 0 || route_id > kMaximumRouteId) {
      encoded = 0;
      return Status::kInvalidArgument;
    }
    const auto priority_bits = static_cast<std::uint16_t>(priority);
    if (priority_bits > 3) {
      encoded = 0;
      return Status::kInvalidArgument;
    }
    encoded = static_cast<std::uint16_t>((priority_bits << 9U) | route_id);
    return Status::kOk;
  }

  static constexpr std::optional<DecodedArbitrationId> Decode(
      std::uint16_t encoded) noexcept {
    if (encoded > 0x7ffU || (encoded & 0x1ffU) == 0) {
      return std::nullopt;
    }
    return DecodedArbitrationId{
        static_cast<CanPriority>((encoded >> 9U) & 0x3U),
        static_cast<std::uint16_t>(encoded & 0x1ffU)};
  }
};

struct CanFrame {
  std::uint16_t arbitration_id{};
  std::uint8_t size{};
  std::array<std::byte, kClassicCanPayloadSize> data{};
};

enum class FrameKind : std::uint8_t {
  kFastSingle = 0,
  kFastFragment = 1,
  kReliable = 2,
  kControl = 3,
};

constexpr FrameKind GetFrameKind(std::byte header) noexcept {
  return static_cast<FrameKind>(
      (std::to_integer<std::uint8_t>(header) >> 6U) & 0x3U);
}

struct ReassembledMessage {
  std::uint16_t route_id{};
  std::uint8_t sequence{};
  std::span<const std::byte> payload;
};

struct LinkStats {
  std::uint32_t tx_frames{};
  std::uint32_t rx_frames{};
  std::uint32_t invalid_frames{};
  std::uint32_t completed_messages{};
  std::uint32_t superseded_messages{};
  std::uint32_t sequence_gaps{};
  std::uint32_t retries{};
  std::uint32_t deadline_misses{};
};

}  // namespace aster::transport::can
