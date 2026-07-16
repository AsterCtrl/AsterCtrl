#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "allocation_tracker.hpp"
#include "aster/transport/can/control_plane.hpp"
#include "aster/transport/can/fast_path.hpp"
#include "aster/transport/can/reliable_path.hpp"

namespace {

using aster::runtime::Status;
using namespace aster::transport::can;

void ArbitrationIdCarriesPriorityAndRoute() {
  std::uint16_t encoded{};
  assert(CanArbitrationId::Encode(CanPriority::kControl, 42, encoded) ==
         Status::kOk);
  assert(encoded == 0x22a);

  const auto decoded = CanArbitrationId::Decode(encoded);
  assert(decoded.has_value());
  assert(decoded->priority == CanPriority::kControl);
  assert(decoded->route_id == 42);
  assert(CanArbitrationId::Encode(CanPriority::kCritical, 512, encoded) ==
         Status::kInvalidArgument);
  assert(CanArbitrationId::Encode(CanPriority::kCritical, 0, encoded) ==
         Status::kInvalidArgument);
}

void FastPathUsesSingleAndFragmentedFixedFrames() {
  constexpr std::array<std::byte, 7> short_payload{
      std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}};
  std::array<CanFrame, 4> frames{};
  std::size_t count{};
  assert(FastCodec::Encode(8, CanPriority::kControl, 5, short_payload, frames,
                           count) == Status::kOk);
  assert(count == 1);
  assert(frames[0].size == 8);
  assert(frames[0].data[0] == std::byte{0x05});

  FastReassembler<32> receiver;
  ReassembledMessage message;
  assert(receiver.Accept(frames[0], message) == Status::kOk);
  assert(message.sequence == 5);
  assert(message.payload.size() == short_payload.size());
  assert(std::equal(message.payload.begin(), message.payload.end(),
                    short_payload.begin()));

  constexpr std::array<std::byte, 13> long_payload{
      std::byte{0},  std::byte{1},  std::byte{2},  std::byte{3},
      std::byte{4},  std::byte{5},  std::byte{6},  std::byte{7},
      std::byte{8},  std::byte{9},  std::byte{10}, std::byte{11},
      std::byte{12}};
  assert(FastCodec::Encode(9, CanPriority::kState, 63, long_payload, frames,
                           count) == Status::kOk);
  assert(count == 3);
  assert(frames[0].data[0] == std::byte{0x7f});
  assert(frames[0].data[1] == std::byte{0x20});
  assert(receiver.Accept(frames[0], message) == Status::kUnavailable);
  assert(receiver.Accept(frames[1], message) == Status::kUnavailable);
  assert(receiver.Accept(frames[2], message) == Status::kOk);
  assert(message.sequence == 63);
  assert(message.payload.size() == long_payload.size());
  assert(std::equal(message.payload.begin(), message.payload.end(),
                    long_payload.begin()));
}

void FastPathDropsAnIncompleteOlderSample() {
  constexpr std::array<std::byte, 8> payload{
      std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}};
  std::array<CanFrame, 2> first{};
  std::array<CanFrame, 2> second{};
  std::size_t first_count{};
  std::size_t second_count{};
  assert(FastCodec::Encode(10, CanPriority::kState, 1, payload, first,
                           first_count) == Status::kOk);
  assert(FastCodec::Encode(10, CanPriority::kState, 2, payload, second,
                           second_count) == Status::kOk);
  FastReassembler<16> receiver;
  ReassembledMessage message;

  assert(receiver.Accept(first[0], message) == Status::kUnavailable);
  assert(receiver.Accept(second[0], message) == Status::kUnavailable);
  assert(receiver.stats().superseded == 1);
  assert(receiver.Accept(second[1], message) == Status::kOk);
}

void FastPathRejectsReplayAndOutOfOrderFrames() {
  constexpr std::array<std::byte, 2> payload{std::byte{0xaa}, std::byte{0xbb}};
  std::array<CanFrame, 1> frame{};
  std::size_t count{};
  FastReassembler<8> receiver;
  ReassembledMessage message;

  assert(FastCodec::Encode(10, CanPriority::kControl, 5, payload, frame,
                           count) == Status::kOk);
  assert(receiver.Accept(frame[0], message) == Status::kOk);
  assert(receiver.Accept(frame[0], message) == Status::kUnavailable);
  assert(message.payload.empty());
  assert(receiver.stats().replayed_frames == 1);

  assert(FastCodec::Encode(10, CanPriority::kControl, 4, payload, frame,
                           count) == Status::kOk);
  assert(receiver.Accept(frame[0], message) == Status::kUnavailable);
  assert(receiver.stats().stale_frames == 1);

  assert(FastCodec::Encode(10, CanPriority::kControl, 7, payload, frame,
                           count) == Status::kOk);
  assert(receiver.Accept(frame[0], message) == Status::kOk);
  assert(receiver.stats().sequence_gaps == 1);

  receiver.ResetHistory();
  assert(FastCodec::Encode(10, CanPriority::kControl, 4, payload, frame,
                           count) == Status::kOk);
  assert(receiver.Accept(frame[0], message) == Status::kOk);
}

void ReliablePathAcknowledgesAndRetriesWholeMessages() {
  constexpr std::array<std::byte, 10> payload{
      std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}, std::byte{9}};
  ReliableSender<32> sender;
  ReliableReceiver<32> receiver;
  assert(sender.Begin(12, CanPriority::kBackground, 3, payload, 1'000, 100,
                      2) == Status::kOk);
  CanFrame frame;
  ReassembledMessage message;
  CanFrame acknowledgement;

  assert(sender.NextFrame(frame) == Status::kOk);
  const auto first_frame = frame;
  assert(receiver.Accept(frame, message, acknowledgement) ==
         Status::kUnavailable);
  assert(sender.NextFrame(frame) == Status::kOk);
  const auto second_frame = frame;
  assert(receiver.Accept(frame, message, acknowledgement) == Status::kOk);
  assert(message.payload.size() == payload.size());
  assert(acknowledgement.size == 1);
  assert(sender.state() == ReliableSenderState::kWaitingForAck);

  ReassembledMessage duplicate;
  CanFrame duplicate_ack;
  assert(receiver.Accept(first_frame, duplicate, duplicate_ack) ==
         Status::kUnavailable);
  assert(receiver.Accept(second_frame, duplicate, duplicate_ack) ==
         Status::kUnavailable);
  assert(duplicate.payload.empty());
  assert(duplicate_ack.size == 1);
  assert(receiver.stats().duplicate_messages == 1);

  assert(sender.Poll(1'099) == Status::kUnavailable);
  assert(sender.Poll(1'100) == Status::kOk);
  assert(sender.stats().retries == 1);
  assert(sender.NextFrame(frame) == Status::kOk);
  assert(sender.HandleAck(acknowledgement) == Status::kOk);
  assert(sender.state() == ReliableSenderState::kComplete);
}

void CompatibilityHeartbeatTimeAndFreshnessAreExplicit() {
  CompatibilityGuard compatibility;
  Handshake local;
  local.protocol_version = 1;
  local.node_id = 1;
  local.deployment_hash[0] = std::byte{0xaa};
  local.schema_hash[0] = std::byte{0xbb};
  auto remote = local;
  remote.node_id = 2;
  assert(compatibility.Check(local, remote) == Compatibility::kCompatible);
  remote.schema_hash[0] = std::byte{0xcc};
  assert(compatibility.Check(local, remote) == Compatibility::kSchemaMismatch);

  HeartbeatMonitor heartbeat(100, 3);
  assert(heartbeat.Observe(1, 0) == LinkState::kRecovering);
  assert(heartbeat.Observe(2, 10) == LinkState::kRecovering);
  assert(heartbeat.Observe(3, 20) == LinkState::kOnline);
  assert(heartbeat.Poll(119) == LinkState::kOnline);
  assert(heartbeat.Poll(120) == LinkState::kLost);
  assert(heartbeat.Observe(8, 130) == LinkState::kRecovering);

  TimeSyncFollower time_sync;
  time_sync.Observe(1'000'000, 900'000);
  assert(time_sync.ToNetworkTime(950'000) == 1'050'000);
  time_sync.Observe(1'110'000, 1'000'000);
  assert(time_sync.offset_ns() > 100'000);
  assert(time_sync.offset_ns() < 110'001);

  FreshnessGuard freshness({10, 30, RearmPolicy::kFreshSample});
  assert(freshness.Observe(1, 100, 105) == FreshnessState::kFresh);
  assert(freshness.Poll(114) == FreshnessState::kFresh);
  assert(freshness.Poll(115) == FreshnessState::kStale);
  assert(freshness.Observe(2, 110, 141) == FreshnessState::kStale);
  assert(freshness.Observe(3, 140, 145) == FreshnessState::kFresh);
}

void ControlPlaneHasStableCompactWireVectors() {
  Handshake handshake;
  handshake.protocol_version = 1;
  handshake.node_id = 2;
  handshake.deployment_hash[0] = std::byte{0xaa};
  handshake.deployment_hash[15] = std::byte{0xab};
  handshake.schema_hash[0] = std::byte{0xba};
  handshake.schema_hash[15] = std::byte{0xbb};
  std::array<std::byte, HandshakeCodec::kEncodedSize> encoded{};
  std::size_t written{};
  assert(HandshakeCodec::Encode(handshake, encoded, written) == Status::kOk);
  assert(written == 34);
  assert(encoded[0] == std::byte{1});
  assert(encoded[1] == std::byte{2});
  assert(encoded[2] == std::byte{0xaa});
  assert(encoded[17] == std::byte{0xab});
  assert(encoded[18] == std::byte{0xba});
  assert(encoded[33] == std::byte{0xbb});
  Handshake decoded;
  assert(HandshakeCodec::Decode(encoded, decoded) == Status::kOk);
  assert(decoded.deployment_hash == handshake.deployment_hash);
  assert(decoded.schema_hash == handshake.schema_hash);

  Heartbeat heartbeat{2, 7, 0x05, 0x12345678};
  CanFrame heartbeat_frame;
  assert(ControlFrameCodec::EncodeHeartbeat(heartbeat, heartbeat_frame) ==
         Status::kOk);
  assert(heartbeat_frame.arbitration_id == kHeartbeatRouteId);
  constexpr std::array<std::byte, 8> heartbeat_vector{
      std::byte{0xc0}, std::byte{0x02}, std::byte{0x07}, std::byte{0x05},
      std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
  assert(heartbeat_frame.data == heartbeat_vector);
  Heartbeat decoded_heartbeat;
  assert(ControlFrameCodec::DecodeHeartbeat(heartbeat_frame,
                                            decoded_heartbeat) == Status::kOk);
  assert(decoded_heartbeat.uptime_ms == heartbeat.uptime_ms);

  TimeSync sync{9, 0x010203040506ULL};
  CanFrame sync_frame;
  assert(ControlFrameCodec::EncodeTimeSync(sync, sync_frame) == Status::kOk);
  assert(sync_frame.arbitration_id == kTimeSyncRouteId);
  constexpr std::array<std::byte, 8> sync_vector{
      std::byte{0xc1}, std::byte{0x09}, std::byte{0x06}, std::byte{0x05},
      std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}};
  assert(sync_frame.data == sync_vector);
  TimeSync decoded_sync;
  assert(ControlFrameCodec::DecodeTimeSync(sync_frame, decoded_sync) ==
         Status::kOk);
  assert(decoded_sync.master_time_us == sync.master_time_us);
}

}  // namespace

int main() {
  const auto allocations = aster_test::AllocationCount();
  ArbitrationIdCarriesPriorityAndRoute();
  FastPathUsesSingleAndFragmentedFixedFrames();
  FastPathDropsAnIncompleteOlderSample();
  FastPathRejectsReplayAndOutOfOrderFrames();
  ReliablePathAcknowledgesAndRetriesWholeMessages();
  CompatibilityHeartbeatTimeAndFreshnessAreExplicit();
  ControlPlaneHasStableCompactWireVectors();
  assert(aster_test::AllocationCount() == allocations);
  return 0;
}
