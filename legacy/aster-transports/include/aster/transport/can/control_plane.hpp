#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/transport/can/protocol.hpp"

namespace aster::transport::can {

inline constexpr std::uint16_t kHandshakeRouteId = 1;
inline constexpr std::uint16_t kHeartbeatRouteId = 2;
inline constexpr std::uint16_t kTimeSyncRouteId = 3;
inline constexpr std::uint16_t kFaultRouteId = 4;

enum class Compatibility : std::uint8_t {
  kCompatible,
  kProtocolMismatch,
  kDeploymentMismatch,
  kSchemaMismatch,
};

struct Handshake {
  std::uint8_t protocol_version{};
  std::uint8_t node_id{};
  std::array<std::byte, 16> deployment_hash{};
  std::array<std::byte, 16> schema_hash{};
};

class HandshakeCodec {
 public:
  static constexpr std::size_t kEncodedSize = 34;

  static Status Encode(const Handshake& handshake, std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kEncodedSize || handshake.protocol_version == 0 ||
        handshake.node_id == 0) {
      return output.size() < kEncodedSize ? Status::kCapacityExceeded
                                          : Status::kInvalidArgument;
    }
    output[0] = static_cast<std::byte>(handshake.protocol_version);
    output[1] = static_cast<std::byte>(handshake.node_id);
    std::copy(handshake.deployment_hash.begin(), handshake.deployment_hash.end(),
              output.begin() + 2);
    std::copy(handshake.schema_hash.begin(), handshake.schema_hash.end(),
              output.begin() + 18);
    written = kEncodedSize;
    return Status::kOk;
  }

  static Status Decode(std::span<const std::byte> input,
                       Handshake& handshake) noexcept {
    if (input.size() != kEncodedSize) {
      return Status::kInvalidArgument;
    }
    handshake.protocol_version = std::to_integer<std::uint8_t>(input[0]);
    handshake.node_id = std::to_integer<std::uint8_t>(input[1]);
    if (handshake.protocol_version == 0 || handshake.node_id == 0) {
      return Status::kInvalidArgument;
    }
    std::copy_n(input.begin() + 2, 16, handshake.deployment_hash.begin());
    std::copy_n(input.begin() + 18, 16, handshake.schema_hash.begin());
    return Status::kOk;
  }
};

struct Heartbeat {
  std::uint8_t node_id{};
  std::uint8_t sequence{};
  std::uint8_t state_flags{};
  std::uint32_t uptime_ms{};
};

struct TimeSync {
  std::uint8_t sequence{};
  std::uint64_t master_time_us{};
};

class ControlFrameCodec {
 public:
  static Status EncodeHeartbeat(const Heartbeat& heartbeat,
                                CanFrame& frame) noexcept {
    if (heartbeat.node_id == 0) {
      return Status::kInvalidArgument;
    }
    frame = {};
    if (const auto status = CanArbitrationId::Encode(
            CanPriority::kCritical, kHeartbeatRouteId, frame.arbitration_id);
        status != Status::kOk) {
      return status;
    }
    frame.size = 8;
    frame.data[0] = std::byte{0xc0};
    frame.data[1] = static_cast<std::byte>(heartbeat.node_id);
    frame.data[2] = static_cast<std::byte>(heartbeat.sequence);
    frame.data[3] = static_cast<std::byte>(heartbeat.state_flags);
    WriteU32(heartbeat.uptime_ms, frame.data.begin() + 4);
    return Status::kOk;
  }

  static Status DecodeHeartbeat(const CanFrame& frame,
                                Heartbeat& heartbeat) noexcept {
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || id->priority != CanPriority::kCritical ||
        id->route_id != kHeartbeatRouteId || frame.size != 8 ||
        frame.data[0] != std::byte{0xc0}) {
      return Status::kInvalidArgument;
    }
    heartbeat.node_id = std::to_integer<std::uint8_t>(frame.data[1]);
    heartbeat.sequence = std::to_integer<std::uint8_t>(frame.data[2]);
    heartbeat.state_flags = std::to_integer<std::uint8_t>(frame.data[3]);
    heartbeat.uptime_ms = ReadU32(frame.data.begin() + 4);
    return heartbeat.node_id == 0 ? Status::kInvalidArgument : Status::kOk;
  }

  static Status EncodeTimeSync(const TimeSync& sync, CanFrame& frame) noexcept {
    if (sync.master_time_us > 0xffffffffffffULL) {
      return Status::kInvalidArgument;
    }
    frame = {};
    if (const auto status = CanArbitrationId::Encode(
            CanPriority::kCritical, kTimeSyncRouteId, frame.arbitration_id);
        status != Status::kOk) {
      return status;
    }
    frame.size = 8;
    frame.data[0] = std::byte{0xc1};
    frame.data[1] = static_cast<std::byte>(sync.sequence);
    for (std::size_t index = 0; index < 6; ++index) {
      frame.data[index + 2] =
          static_cast<std::byte>(sync.master_time_us >> (index * 8U));
    }
    return Status::kOk;
  }

  static Status DecodeTimeSync(const CanFrame& frame, TimeSync& sync) noexcept {
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || id->priority != CanPriority::kCritical ||
        id->route_id != kTimeSyncRouteId || frame.size != 8 ||
        frame.data[0] != std::byte{0xc1}) {
      return Status::kInvalidArgument;
    }
    sync.sequence = std::to_integer<std::uint8_t>(frame.data[1]);
    sync.master_time_us = 0;
    for (std::size_t index = 0; index < 6; ++index) {
      sync.master_time_us |=
          static_cast<std::uint64_t>(
              std::to_integer<std::uint8_t>(frame.data[index + 2]))
          << (index * 8U);
    }
    return Status::kOk;
  }

 private:
  static void WriteU32(std::uint32_t value,
                       std::array<std::byte, 8>::iterator output) noexcept {
    for (std::size_t index = 0; index < 4; ++index) {
      output[static_cast<std::ptrdiff_t>(index)] =
          static_cast<std::byte>(value >> (index * 8U));
    }
  }

  static std::uint32_t ReadU32(
      std::array<std::byte, 8>::const_iterator input) noexcept {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
      value |= static_cast<std::uint32_t>(
                   std::to_integer<std::uint8_t>(
                       input[static_cast<std::ptrdiff_t>(index)]))
               << (index * 8U);
    }
    return value;
  }
};

class CompatibilityGuard {
 public:
  constexpr Compatibility Check(const Handshake& local,
                                const Handshake& remote) const noexcept {
    if (local.protocol_version != remote.protocol_version) {
      return Compatibility::kProtocolMismatch;
    }
    if (local.deployment_hash != remote.deployment_hash) {
      return Compatibility::kDeploymentMismatch;
    }
    if (local.schema_hash != remote.schema_hash) {
      return Compatibility::kSchemaMismatch;
    }
    return Compatibility::kCompatible;
  }
};

enum class LinkState : std::uint8_t {
  kLost,
  kRecovering,
  kOnline,
};

class HeartbeatMonitor {
 public:
  constexpr HeartbeatMonitor(std::uint64_t timeout_ns,
                             std::uint8_t recovery_samples) noexcept
      : timeout_ns_(timeout_ns), recovery_samples_(recovery_samples) {}

  LinkState Observe(std::uint8_t sequence, std::uint64_t now_ns) noexcept {
    const auto expected = static_cast<std::uint8_t>(last_sequence_ + 1U);
    if (!has_observation_ || state_ == LinkState::kLost || sequence != expected) {
      consecutive_samples_ = 1;
    } else if (consecutive_samples_ < recovery_samples_) {
      ++consecutive_samples_;
    }
    has_observation_ = true;
    last_sequence_ = sequence;
    last_observation_ns_ = now_ns;
    state_ = consecutive_samples_ >= recovery_samples_ ? LinkState::kOnline
                                                        : LinkState::kRecovering;
    return state_;
  }

  LinkState Poll(std::uint64_t now_ns) noexcept {
    if (!has_observation_ || now_ns - last_observation_ns_ >= timeout_ns_) {
      state_ = LinkState::kLost;
      consecutive_samples_ = 0;
    }
    return state_;
  }

  LinkState state() const noexcept { return state_; }

 private:
  std::uint64_t timeout_ns_{};
  std::uint64_t last_observation_ns_{};
  std::uint8_t recovery_samples_{};
  std::uint8_t consecutive_samples_{};
  std::uint8_t last_sequence_{};
  bool has_observation_{};
  LinkState state_{LinkState::kLost};
};

class TimeSyncFollower {
 public:
  void Observe(std::uint64_t master_time_ns,
               std::uint64_t local_receive_time_ns) noexcept {
    const auto sample = static_cast<std::int64_t>(master_time_ns) -
                        static_cast<std::int64_t>(local_receive_time_ns);
    if (!synchronized_) {
      offset_ns_ = sample;
      synchronized_ = true;
      return;
    }
    offset_ns_ += (sample - offset_ns_) / 8;
  }

  std::uint64_t ToNetworkTime(std::uint64_t local_time_ns) const noexcept {
    if (!synchronized_) {
      return local_time_ns;
    }
    const auto converted = static_cast<std::int64_t>(local_time_ns) + offset_ns_;
    return converted <= 0 ? 0 : static_cast<std::uint64_t>(converted);
  }

  std::int64_t offset_ns() const noexcept { return offset_ns_; }
  bool synchronized() const noexcept { return synchronized_; }

 private:
  std::int64_t offset_ns_{};
  bool synchronized_{};
};

enum class RearmPolicy : std::uint8_t {
  kAutomatic,
  kFreshSample,
  kExplicit,
};

enum class FreshnessState : std::uint8_t {
  kUnarmed,
  kFresh,
  kStale,
};

struct FreshnessConfig {
  std::uint64_t deadline_ns{};
  std::uint64_t max_age_ns{};
  RearmPolicy rearm{RearmPolicy::kFreshSample};
};

class FreshnessGuard {
 public:
  constexpr explicit FreshnessGuard(FreshnessConfig config) noexcept
      : config_(config) {}

  FreshnessState Observe(std::uint8_t sequence,
                         std::uint64_t source_timestamp_ns,
                         std::uint64_t receive_timestamp_ns) noexcept {
    const auto age = source_timestamp_ns > receive_timestamp_ns
                         ? 0
                         : receive_timestamp_ns - source_timestamp_ns;
    last_sequence_ = sequence;
    last_receive_ns_ = receive_timestamp_ns;
    has_sample_ = true;
    if (config_.max_age_ns != 0 && age > config_.max_age_ns) {
      state_ = FreshnessState::kStale;
      return state_;
    }
    if (state_ == FreshnessState::kStale &&
        config_.rearm == RearmPolicy::kExplicit && !explicitly_rearmed_) {
      return state_;
    }
    explicitly_rearmed_ = false;
    state_ = FreshnessState::kFresh;
    return state_;
  }

  FreshnessState Poll(std::uint64_t now_ns) noexcept {
    if (!has_sample_) {
      return state_;
    }
    if (config_.deadline_ns != 0 &&
        now_ns - last_receive_ns_ >= config_.deadline_ns) {
      state_ = FreshnessState::kStale;
    }
    return state_;
  }

  void Rearm() noexcept { explicitly_rearmed_ = true; }
  FreshnessState state() const noexcept { return state_; }
  std::uint8_t last_sequence() const noexcept { return last_sequence_; }

 private:
  FreshnessConfig config_;
  std::uint64_t last_receive_ns_{};
  std::uint8_t last_sequence_{};
  bool has_sample_{};
  bool explicitly_rearmed_{};
  FreshnessState state_{FreshnessState::kUnarmed};
};

}  // namespace aster::transport::can
