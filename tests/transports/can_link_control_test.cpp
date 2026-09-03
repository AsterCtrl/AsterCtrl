#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "allocation_tracker.hpp"
#include "aster/transport/can/link_control.hpp"

namespace {

using aster::ExecutionContext;
using aster::ExecutionKind;
using aster::Status;
using namespace aster::transport::can;

struct Clock {
  std::uint64_t now_ns{1'000'000};
};

std::uint64_t ReadClock(void* state) noexcept { return static_cast<Clock*>(state)->now_ns; }

struct QueuedFrame {
  CanFrame frame;
  bool to_b{};
};

struct Bus {
  CanLinkControlPlane* a{};
  CanLinkControlPlane* b{};
  Clock* clock{};
  const ExecutionContext* context{};
  std::array<QueuedFrame, 64> frames{};
  std::size_t size{};
  std::uint32_t reject_a_to_b{};
  std::uint32_t reject_b_to_a{};
  CanFrame last_rejected{};

  Status Queue(const CanFrame& frame, bool to_b) noexcept {
    auto& rejected = to_b ? reject_a_to_b : reject_b_to_a;
    if (rejected != 0) {
      --rejected;
      last_rejected = frame;
      return Status::kCapacityExceeded;
    }
    if (size == frames.size()) return Status::kCapacityExceeded;
    frames[size++] = {frame, to_b};
    return Status::kOk;
  }

  void Pump() noexcept {
    while (size != 0) {
      const auto count = size;
      size = 0;
      for (std::size_t index = 0; index < count; ++index) {
        auto& target = frames[index].to_b ? *b : *a;
        target.Accept(frames[index].frame, clock->now_ns, *context);
      }
    }
  }
};

Status AToB(void* state, const CanFrame& frame, const ExecutionContext&) noexcept {
  return static_cast<Bus*>(state)->Queue(frame, true);
}

struct Sink {
  std::uint32_t frames{};
};

Status WriteToSink(void* state, const CanFrame&, const ExecutionContext&) noexcept {
  ++static_cast<Sink*>(state)->frames;
  return Status::kOk;
}

Status BToA(void* state, const CanFrame& frame, const ExecutionContext&) noexcept {
  return static_cast<Bus*>(state)->Queue(frame, false);
}

Handshake LocalHandshake(std::uint8_t node_id, std::byte schema = std::byte{2}) {
  Handshake handshake;
  handshake.protocol_version = 1;
  handshake.node_id = node_id;
  handshake.deployment_hash.fill(std::byte{1});
  handshake.schema_hash.fill(schema);
  return handshake;
}

CanLinkControlConfig Config(std::uint8_t local, std::uint8_t peer, bool authority,
                            std::byte schema = std::byte{2}) {
  return {
      .local = LocalHandshake(local, schema),
      .peer_node_id = peer,
      .time_authority = authority,
      .handshake_period_ns = 100,
      .heartbeat_period_ns = 100,
      .heartbeat_timeout_ns = 250,
      .time_sync_period_ns = 100,
      .recovery_samples = 1,
      .retry_timeout_ns = 50,
      .maximum_retries = 2,
      .reassembly_timeout_ns = 40,
  };
}

void PollPair(CanLinkControlPlane& a, CanLinkControlPlane& b, Bus& bus, Clock& clock,
              const ExecutionContext& context) {
  assert(a.Poll(clock.now_ns, context) == Status::kOk);
  assert(b.Poll(clock.now_ns, context) == Status::kOk);
  bus.Pump();
}

void CompatiblePeersEnableApplicationTrafficAndNetworkTime() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Bus bus;
  CanLinkControlPlane a(Config(1, 2, true), {AToB, &bus}, {ReadClock, &clock});
  CanLinkControlPlane b(Config(2, 1, false), {BToA, &bus}, {ReadClock, &clock});
  bus = {&a, &b, &clock, &context};

  CanFrame application;
  assert(CanArbitrationId::Encode(CanPriority::kControl, 8, application.arbitration_id) ==
         Status::kOk);
  application.size = 1;
  application.data[0] = std::byte{0x01};
  assert(a.application_writer().Send(application, context) == Status::kUnavailable);

  PollPair(a, b, bus, clock, context);
  assert(a.compatible());
  assert(b.compatible());
  assert(a.application_enabled());
  assert(b.application_enabled());
  assert(b.time_synchronized());
  assert(b.ToNetworkTime(clock.now_ns) == clock.now_ns);
  assert(a.application_writer().Send(application, context) == Status::kOk);
  assert(bus.size == 1);
}

void SchemaMismatchNeverEnablesApplicationTraffic() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Bus bus;
  CanLinkControlPlane a(Config(1, 2, true), {AToB, &bus}, {ReadClock, &clock});
  CanLinkControlPlane b(Config(2, 1, false, std::byte{3}), {BToA, &bus}, {ReadClock, &clock});
  bus = {&a, &b, &clock, &context};

  PollPair(a, b, bus, clock, context);
  assert(!a.compatible());
  assert(!b.compatible());
  assert(a.compatibility() == Compatibility::kSchemaMismatch);
  assert(b.compatibility() == Compatibility::kSchemaMismatch);
  assert(!a.application_enabled());
  assert(!b.application_enabled());
}

void HeartbeatTimeoutClosesAndFreshTrafficReopensTheGate() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Bus bus;
  CanLinkControlPlane a(Config(1, 2, true), {AToB, &bus}, {ReadClock, &clock});
  CanLinkControlPlane b(Config(2, 1, false), {BToA, &bus}, {ReadClock, &clock});
  bus = {&a, &b, &clock, &context};
  PollPair(a, b, bus, clock, context);
  assert(b.application_enabled());

  clock.now_ns += 251;
  bus.reject_b_to_a = 1;
  assert(b.Poll(clock.now_ns, context) == Status::kCapacityExceeded);
  assert(!b.application_enabled());

  assert(a.Poll(clock.now_ns, context) == Status::kOk);
  bus.Pump();
  assert(b.application_enabled());
}

void HandshakeBackpressureRetriesTheRejectedFrame() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Bus bus;
  CanLinkControlPlane a(Config(1, 2, true), {AToB, &bus}, {ReadClock, &clock});
  CanLinkControlPlane b(Config(2, 1, false), {BToA, &bus}, {ReadClock, &clock});
  bus = {&a, &b, &clock, &context};
  bus.reject_a_to_b = 1;

  assert(a.Poll(clock.now_ns, context) == Status::kCapacityExceeded);
  assert(a.stats().handshake_backpressure == 1);
  assert(bus.size == 0);
  const auto rejected = bus.last_rejected;
  assert(rejected.write_completion == nullptr);
  assert(rejected.write_state == nullptr);
  assert(rejected.write_token == 0);

  assert(a.Poll(clock.now_ns, context) == Status::kOk);
  assert(bus.size != 0);
  assert(bus.frames[0].frame.arbitration_id == rejected.arbitration_id);
  assert(bus.frames[0].frame.size == rejected.size);
  assert(bus.frames[0].frame.data == rejected.data);
}

void IncompleteHandshakeExpiresWithoutAllocating() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Sink sink;
  CanLinkControlPlane receiver(Config(1, 2, true), {WriteToSink, &sink}, {ReadClock, &clock});
  const auto remote = LocalHandshake(2);
  std::array<std::byte, HandshakeCodec::kEncodedSize> payload{};
  std::size_t written{};
  assert(HandshakeCodec::Encode(remote, payload, written) == Status::kOk);
  assert(written == payload.size());
  ReliableSender<HandshakeCodec::kEncodedSize> sender;
  assert(sender.Begin(kHandshakeRouteId, CanPriority::kCritical, 3, payload, clock.now_ns, 50, 1) ==
         Status::kOk);
  CanFrame first_fragment;
  assert(sender.NextFrame(first_fragment) == Status::kOk);

  assert(receiver.Accept(first_fragment, clock.now_ns, context) == Status::kUnavailable);
  clock.now_ns += 39;
  assert(receiver.Poll(clock.now_ns, context) == Status::kOk);
  assert(receiver.stats().handshake_reassembly_timeouts == 0);
  clock.now_ns += 1;
  assert(receiver.Poll(clock.now_ns, context) == Status::kOk);
  assert(receiver.stats().handshake_reassembly_timeouts == 1);
}

void PeerRestartClosesTheGateUntilAHandshakeIsReceivedAgain() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Bus bus;
  CanLinkControlPlane a(Config(1, 2, true), {AToB, &bus}, {ReadClock, &clock});
  CanLinkControlPlane b(Config(2, 1, false), {BToA, &bus}, {ReadClock, &clock});
  bus = {&a, &b, &clock, &context};
  PollPair(a, b, bus, clock, context);
  assert(a.application_enabled());

  CanFrame restart_heartbeat;
  assert(ControlFrameCodec::EncodeHeartbeat({2, 0, 0, 0}, restart_heartbeat) == Status::kOk);
  assert(a.Accept(restart_heartbeat, clock.now_ns, context) == Status::kOk);
  assert(a.stats().peer_restarts == 1);
  assert(!a.compatible());
  assert(!a.application_enabled());

  CanFrame follower_restart_heartbeat;
  assert(ControlFrameCodec::EncodeHeartbeat({1, 0, 0, 0}, follower_restart_heartbeat) ==
         Status::kOk);
  assert(b.Accept(follower_restart_heartbeat, clock.now_ns, context) == Status::kOk);
  assert(b.stats().peer_restarts == 1);
  assert(!b.time_synchronized());
  assert(!b.application_enabled());

  clock.now_ns += 100;
  PollPair(a, b, bus, clock, context);
  assert(a.compatible());
  assert(a.application_enabled());

  CanFrame duplicate;
  assert(ControlFrameCodec::EncodeHeartbeat({2, 1, 0, 1}, duplicate) == Status::kOk);
  assert(a.Accept(duplicate, clock.now_ns, context) == Status::kUnavailable);
  assert(a.stats().duplicate_heartbeats == 1);
  CanFrame stale;
  assert(ControlFrameCodec::EncodeHeartbeat({2, 0, 0, 1}, stale) == Status::kOk);
  assert(a.Accept(stale, clock.now_ns, context) == Status::kUnavailable);
  assert(a.stats().stale_heartbeats == 1);
}

void PeerRestartCancelsAnInFlightHandshake() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Sink sink;
  CanLinkControlPlane link(Config(1, 2, true), {WriteToSink, &sink}, {ReadClock, &clock});
  assert(link.Poll(clock.now_ns, context) == Status::kOk);
  assert(link.stats().handshake_messages == 1);

  CanFrame heartbeat;
  assert(ControlFrameCodec::EncodeHeartbeat({2, 9, 0, 10}, heartbeat) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kOk);
  assert(ControlFrameCodec::EncodeHeartbeat({2, 0, 0, 0}, heartbeat) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kOk);
  assert(link.stats().peer_restarts == 1);

  assert(link.Poll(clock.now_ns, context) == Status::kOk);
  assert(link.stats().handshake_messages == 2);
}

void HeartbeatOrderingUsesUptimeAcrossSequenceWraps() {
  Clock clock;
  const ExecutionContext context("link", ExecutionKind::kThread, 7);
  Sink sink;
  CanLinkControlPlane link(Config(1, 2, true), {WriteToSink, &sink}, {ReadClock, &clock});
  CanFrame heartbeat;

  assert(ControlFrameCodec::EncodeHeartbeat({2, 200, 0, 100}, heartbeat) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kOk);
  assert(ControlFrameCodec::EncodeHeartbeat({2, 100, 0, 1'000}, heartbeat) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kOk);
  assert(ControlFrameCodec::EncodeHeartbeat({2, 100, 0, 2'000}, heartbeat) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kUnavailable);
  assert(link.stats().duplicate_heartbeats == 1);
  assert(ControlFrameCodec::EncodeHeartbeat({2, 99, 0, 2'000}, heartbeat) == Status::kOk);
  assert(link.Accept(heartbeat, clock.now_ns, context) == Status::kUnavailable);
  assert(link.stats().stale_heartbeats == 1);
}

}  // namespace

int main() {
  const auto allocations = aster_test::AllocationCount();
  CompatiblePeersEnableApplicationTrafficAndNetworkTime();
  SchemaMismatchNeverEnablesApplicationTraffic();
  HeartbeatTimeoutClosesAndFreshTrafficReopensTheGate();
  HandshakeBackpressureRetriesTheRejectedFrame();
  IncompleteHandshakeExpiresWithoutAllocating();
  PeerRestartClosesTheGateUntilAHandshakeIsReceivedAgain();
  PeerRestartCancelsAnInFlightHandshake();
  HeartbeatOrderingUsesUptimeAcrossSequenceWraps();
  assert(aster_test::AllocationCount() == allocations);
  return 0;
}
