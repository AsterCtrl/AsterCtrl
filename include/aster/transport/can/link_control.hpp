#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "aster/execution.hpp"
#include "aster/status.hpp"
#include "aster/transport/can/control_plane.hpp"
#include "aster/transport/can/link.hpp"
#include "aster/transport/can/reliable_path.hpp"

namespace aster::transport::can {

struct CanLinkControlConfig {
  Handshake local;
  std::uint8_t peer_node_id{};
  bool time_authority{};
  std::uint64_t handshake_period_ns{1'000'000'000};
  std::uint64_t heartbeat_period_ns{100'000'000};
  std::uint64_t heartbeat_timeout_ns{300'000'000};
  std::uint64_t time_sync_period_ns{10'000'000};
  std::uint8_t recovery_samples{3};
  std::uint64_t retry_timeout_ns{20'000'000};
  std::uint8_t maximum_retries{2};
  std::uint64_t reassembly_timeout_ns{100'000'000};
};

struct CanLinkControlStats {
  std::uint32_t handshake_messages{};
  std::uint32_t handshake_retries{};
  std::uint32_t heartbeat_messages{};
  std::uint32_t time_sync_messages{};
  std::uint32_t received_control_frames{};
  std::uint32_t invalid_control_frames{};
  std::uint32_t blocked_application_frames{};
  std::uint32_t forwarded_application_frames{};
  std::uint32_t handshake_failures{};
  std::uint32_t handshake_backpressure{};
  std::uint32_t handshake_reassembly_timeouts{};
  std::uint32_t duplicate_heartbeats{};
  std::uint32_t stale_heartbeats{};
  std::uint32_t peer_restarts{};
};

class CanLinkControlPlane {
 public:
  CanLinkControlPlane(CanLinkControlConfig config, CanFrameWriter physical_writer,
                      CanClockReader local_clock) noexcept
      : config_(config),
        physical_writer_(physical_writer),
        local_clock_(local_clock),
        heartbeat_(config.heartbeat_timeout_ns, config.recovery_samples),
        handshake_receiver_(config.reassembly_timeout_ns) {
    std::size_t written{};
    valid_ = ConfigValid() &&
             HandshakeCodec::Encode(config_.local, handshake_payload_, written) == Status::kOk &&
             written == handshake_payload_.size();
  }

  CanLinkControlPlane(const CanLinkControlPlane&) = delete;
  CanLinkControlPlane& operator=(const CanLinkControlPlane&) = delete;
  CanLinkControlPlane(CanLinkControlPlane&&) = delete;
  CanLinkControlPlane& operator=(CanLinkControlPlane&&) = delete;

  Status Poll(std::uint64_t now_ns, const aster::ExecutionContext& caller) noexcept {
    using aster::ExecutionKind;

    if (!valid_) return Status::kInvalidArgument;
    if (caller.kind() == ExecutionKind::kInterrupt || (has_last_poll_ && now_ns < last_poll_ns_)) {
      return Status::kInvalidArgument;
    }
    has_last_poll_ = true;
    last_poll_ns_ = now_ns;
    heartbeat_.Poll(now_ns);
    RefreshApplicationGate();

    const auto receiver_status = handshake_receiver_.Poll(now_ns);
    if (receiver_status == Status::kTimeout) {
      ++stats_.handshake_reassembly_timeouts;
    } else if (receiver_status != Status::kUnavailable) {
      return receiver_status;
    }

    if (handshake_sender_.state() == ReliableSenderState::kSending) {
      if (const auto status = PumpHandshake(now_ns, caller); status != Status::kOk) {
        return status;
      }
    }

    if (handshake_sender_.state() == ReliableSenderState::kWaitingForAck) {
      const auto retry_status = handshake_sender_.Poll(now_ns);
      if (retry_status == Status::kOk) {
        ++stats_.handshake_retries;
        if (const auto status = PumpHandshake(now_ns, caller); status != Status::kOk) {
          return status;
        }
      } else if (retry_status == Status::kTimeout) {
        ++stats_.handshake_failures;
      } else if (retry_status != Status::kUnavailable) {
        return retry_status;
      }
    }

    if (!handshake_scheduled_ || now_ns >= next_handshake_ns_) {
      if (!SenderBusy(handshake_sender_.state())) {
        const auto status = handshake_sender_.Begin(
            kHandshakeRouteId, CanPriority::kCritical, handshake_sequence_, handshake_payload_,
            now_ns, config_.retry_timeout_ns, config_.maximum_retries);
        if (status != Status::kOk) return status;
        handshake_sequence_ = static_cast<std::uint8_t>((handshake_sequence_ + 1U) & 0x0fU);
        if (const auto pump_status = PumpHandshake(now_ns, caller); pump_status != Status::kOk) {
          return pump_status;
        }
        ++stats_.handshake_messages;
      }
      handshake_scheduled_ = true;
      next_handshake_ns_ = now_ns + config_.handshake_period_ns;
    }

    if (!heartbeat_scheduled_ || now_ns >= next_heartbeat_ns_) {
      CanFrame frame;
      const Heartbeat heartbeat{
          config_.local.node_id,
          heartbeat_sequence_++,
          StateFlags(),
          static_cast<std::uint32_t>((now_ns / 1'000'000U) & 0xffffffffU),
      };
      if (const auto status = ControlFrameCodec::EncodeHeartbeat(heartbeat, frame);
          status != Status::kOk) {
        return status;
      }
      if (const auto status = physical_writer_.Send(frame, caller); status != Status::kOk) {
        return status;
      }
      ++stats_.heartbeat_messages;
      heartbeat_scheduled_ = true;
      next_heartbeat_ns_ = now_ns + config_.heartbeat_period_ns;
    }

    if (config_.time_authority && (!time_sync_scheduled_ || now_ns >= next_time_sync_ns_)) {
      CanFrame frame;
      const TimeSync sync{
          time_sync_sequence_++,
          (now_ns / 1'000U) & 0xffffffffffffULL,
      };
      if (const auto status = ControlFrameCodec::EncodeTimeSync(sync, frame);
          status != Status::kOk) {
        return status;
      }
      if (const auto status = physical_writer_.Send(frame, caller); status != Status::kOk) {
        return status;
      }
      ++stats_.time_sync_messages;
      time_sync_scheduled_ = true;
      next_time_sync_ns_ = now_ns + config_.time_sync_period_ns;
    }

    return Status::kOk;
  }

  Status Accept(const CanFrame& frame, std::uint64_t receive_local_time_ns,
                const aster::ExecutionContext& caller) noexcept {
    if (!valid_) return Status::kInvalidArgument;
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || id->priority != CanPriority::kCritical) {
      return InvalidControlFrame();
    }

    Status status{Status::kUnavailable};
    switch (id->route_id) {
      case kHandshakeRouteId:
        status = AcceptHandshake(frame, receive_local_time_ns, caller);
        break;
      case kHeartbeatRouteId:
        status = AcceptHeartbeat(frame, receive_local_time_ns);
        break;
      case kTimeSyncRouteId:
        status = AcceptTimeSync(frame, receive_local_time_ns);
        break;
      default:
        return Status::kUnavailable;
    }
    ++stats_.received_control_frames;
    if (status != Status::kOk && status != Status::kUnavailable) {
      ++stats_.invalid_control_frames;
    }
    RefreshApplicationGate();
    return status;
  }

  CanFrameWriter application_writer() noexcept { return {WriteApplication, this}; }
  CanTimeConverter time_converter() noexcept { return {ConvertTime, this}; }

  bool compatible() const noexcept {
    return handshake_received_ && compatibility_ == Compatibility::kCompatible;
  }
  Compatibility compatibility() const noexcept { return compatibility_; }
  LinkState heartbeat_state() const noexcept { return heartbeat_.state(); }
  bool time_synchronized() const noexcept {
    return config_.time_authority || time_sync_.synchronized();
  }
  bool application_enabled() const noexcept { return application_enabled_; }
  std::uint64_t ToNetworkTime(std::uint64_t local_time_ns) const noexcept {
    return config_.time_authority ? local_time_ns : time_sync_.ToNetworkTime(local_time_ns);
  }
  const CanLinkControlStats& stats() const noexcept { return stats_; }

 private:
  static bool SenderBusy(ReliableSenderState state) noexcept {
    return state == ReliableSenderState::kSending || state == ReliableSenderState::kWaitingForAck;
  }

  bool ConfigValid() const noexcept {
    return config_.local.protocol_version != 0 && config_.local.node_id != 0 &&
           config_.peer_node_id != 0 && config_.peer_node_id != config_.local.node_id &&
           config_.handshake_period_ns != 0 && config_.heartbeat_period_ns != 0 &&
           config_.heartbeat_timeout_ns > config_.heartbeat_period_ns &&
           config_.time_sync_period_ns != 0 && config_.recovery_samples != 0 &&
           config_.retry_timeout_ns != 0 && config_.reassembly_timeout_ns != 0 &&
           physical_writer_.write != nullptr && local_clock_.read != nullptr;
  }

  std::uint8_t StateFlags() const noexcept {
    return static_cast<std::uint8_t>((compatible() ? 0x01U : 0U) |
                                     (application_enabled_ ? 0x02U : 0U) |
                                     (time_synchronized() ? 0x04U : 0U));
  }

  Status PumpHandshake(std::uint64_t now_ns, const aster::ExecutionContext& caller) noexcept {
    CanFrame frame;
    bool sent_frame{};
    while (handshake_sender_.NextFrame(frame) == Status::kOk) {
      const auto status = physical_writer_.Send(frame, caller);
      if (status != Status::kOk) {
        if (status == Status::kUnavailable || status == Status::kCapacityExceeded) {
          ++stats_.handshake_backpressure;
        }
        return status;
      }
      sent_frame = true;
    }
    if (sent_frame && handshake_sender_.state() == ReliableSenderState::kWaitingForAck) {
      return handshake_sender_.ArmAckDeadline(now_ns);
    }
    return Status::kOk;
  }

  Status AcceptHandshake(const CanFrame& frame, std::uint64_t receive_local_time_ns,
                         const aster::ExecutionContext& caller) noexcept {
    if (frame.size == 0 || GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      return handshake_sender_.HandleAck(frame);
    }

    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto status =
        handshake_receiver_.Accept(frame, receive_local_time_ns, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = physical_writer_.Send(acknowledgement, caller);
      if (ack_status != Status::kOk) return ack_status;
    }
    if (status != Status::kOk) return status;

    Handshake remote;
    if (HandshakeCodec::Decode(message.payload, remote) != Status::kOk ||
        remote.node_id != config_.peer_node_id) {
      return Status::kTypeMismatch;
    }
    handshake_received_ = true;
    compatibility_ = compatibility_guard_.Check(config_.local, remote);
    return compatibility_ == Compatibility::kCompatible ? Status::kOk : Status::kTypeMismatch;
  }

  Status AcceptHeartbeat(const CanFrame& frame, std::uint64_t receive_local_time_ns) noexcept {
    Heartbeat heartbeat;
    if (ControlFrameCodec::DecodeHeartbeat(frame, heartbeat) != Status::kOk ||
        heartbeat.node_id != config_.peer_node_id) {
      return Status::kTypeMismatch;
    }
    if (has_peer_heartbeat_) {
      const auto sequence_distance =
          static_cast<std::uint8_t>(heartbeat.sequence - peer_heartbeat_sequence_);
      const auto uptime_distance =
          static_cast<std::uint32_t>(heartbeat.uptime_ms - peer_uptime_ms_);
      const auto uptime_moved_backward = uptime_distance > UINT32_MAX / 2U;
      const auto restart_sequence = heartbeat.sequence <= 1U;
      if (uptime_moved_backward && (sequence_distance <= 127U || restart_sequence)) {
        HandlePeerRestart();
      } else if (uptime_moved_backward) {
        ++stats_.stale_heartbeats;
        return Status::kUnavailable;
      } else if (uptime_distance == 0U && sequence_distance == 0U) {
        ++stats_.duplicate_heartbeats;
        return Status::kUnavailable;
      } else if (uptime_distance == 0U && sequence_distance > 127U) {
        ++stats_.stale_heartbeats;
        return Status::kUnavailable;
      }
    }
    has_peer_heartbeat_ = true;
    peer_heartbeat_sequence_ = heartbeat.sequence;
    peer_uptime_ms_ = heartbeat.uptime_ms;
    heartbeat_.Observe(heartbeat.sequence, receive_local_time_ns);
    return Status::kOk;
  }

  void HandlePeerRestart() noexcept {
    ++stats_.peer_restarts;
    handshake_received_ = false;
    compatibility_ = Compatibility::kProtocolMismatch;
    application_enabled_ = false;
    handshake_scheduled_ = false;
    handshake_sender_ = {};
    handshake_receiver_.ResetPeerHistory();
    heartbeat_ = HeartbeatMonitor(config_.heartbeat_timeout_ns, config_.recovery_samples);
    time_sync_ = {};
  }

  Status AcceptTimeSync(const CanFrame& frame, std::uint64_t receive_local_time_ns) noexcept {
    if (config_.time_authority) return Status::kUnavailable;
    TimeSync sync;
    if (ControlFrameCodec::DecodeTimeSync(frame, sync) != Status::kOk) {
      return Status::kInvalidArgument;
    }
    time_sync_.Observe(sync.master_time_us * 1'000U, receive_local_time_ns);
    return Status::kOk;
  }

  static Status WriteApplication(void* state, const CanFrame& frame,
                                 const aster::ExecutionContext& caller) noexcept {
    auto& self = *static_cast<CanLinkControlPlane*>(state);
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || id->route_id < kFirstApplicationRouteId) {
      return Status::kInvalidArgument;
    }
    if (!self.application_enabled_) {
      ++self.stats_.blocked_application_frames;
      return Status::kUnavailable;
    }
    const auto status = self.physical_writer_.Send(frame, caller);
    if (status == Status::kOk) {
      ++self.stats_.forwarded_application_frames;
    }
    return status;
  }

  static std::uint64_t ConvertTime(void* state, std::uint64_t local_time_ns) noexcept {
    return static_cast<CanLinkControlPlane*>(state)->ToNetworkTime(local_time_ns);
  }

  Status InvalidControlFrame() noexcept {
    ++stats_.invalid_control_frames;
    return Status::kInvalidArgument;
  }

  void RefreshApplicationGate() noexcept {
    application_enabled_ =
        compatible() && heartbeat_.state() == LinkState::kOnline && time_synchronized();
  }

  CanLinkControlConfig config_;
  CanFrameWriter physical_writer_;
  CanClockReader local_clock_;
  HeartbeatMonitor heartbeat_;
  TimeSyncFollower time_sync_;
  CompatibilityGuard compatibility_guard_;
  ReliableSender<HandshakeCodec::kEncodedSize> handshake_sender_;
  ReliableReceiver<HandshakeCodec::kEncodedSize> handshake_receiver_;
  std::array<std::byte, HandshakeCodec::kEncodedSize> handshake_payload_{};
  std::uint64_t next_handshake_ns_{};
  std::uint64_t next_heartbeat_ns_{};
  std::uint64_t next_time_sync_ns_{};
  std::uint64_t last_poll_ns_{};
  Compatibility compatibility_{Compatibility::kProtocolMismatch};
  std::uint8_t handshake_sequence_{};
  std::uint8_t heartbeat_sequence_{};
  std::uint8_t time_sync_sequence_{};
  std::uint8_t peer_heartbeat_sequence_{};
  std::uint32_t peer_uptime_ms_{};
  bool valid_{};
  bool handshake_received_{};
  bool application_enabled_{};
  bool handshake_scheduled_{};
  bool heartbeat_scheduled_{};
  bool time_sync_scheduled_{};
  bool has_last_poll_{};
  bool has_peer_heartbeat_{};
  CanLinkControlStats stats_{};
};

}  // namespace aster::transport::can
