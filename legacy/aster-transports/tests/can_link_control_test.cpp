#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "allocation_tracker.hpp"
#include "xrobot/transport/can/link_control.hpp"

namespace {

using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::Status;
using namespace xrobot::transport::can;

struct Clock {
  std::uint64_t now_ns{1'000'000};
};

std::uint64_t ReadClock(void* state) noexcept {
  return static_cast<Clock*>(state)->now_ns;
}

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

  Status Queue(const CanFrame& frame, bool to_b) noexcept {
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

Status AToB(void* state, const CanFrame& frame,
            const ExecutionContext&) noexcept {
  return static_cast<Bus*>(state)->Queue(frame, true);
}

Status BToA(void* state, const CanFrame& frame,
            const ExecutionContext&) noexcept {
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

CanLinkControlConfig Config(std::uint8_t local, std::uint8_t peer,
                            bool authority,
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
  };
}

void PollPair(CanLinkControlPlane& a, CanLinkControlPlane& b, Bus& bus,
              Clock& clock, const ExecutionContext& context) {
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
  assert(CanArbitrationId::Encode(CanPriority::kControl, 8,
                                  application.arbitration_id) == Status::kOk);
  application.size = 1;
  application.data[0] = std::byte{0x01};
  assert(a.application_writer().Send(application, context) ==
         Status::kUnavailable);

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
  CanLinkControlPlane b(
      Config(2, 1, false, std::byte{3}), {BToA, &bus}, {ReadClock, &clock});
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
  assert(b.Poll(clock.now_ns, context) == Status::kOk);
  bus.Pump();
  assert(!b.application_enabled());

  assert(a.Poll(clock.now_ns, context) == Status::kOk);
  bus.Pump();
  assert(b.application_enabled());
}

}  // namespace

int main() {
  const auto allocations = xrobot_test::AllocationCount();
  CompatiblePeersEnableApplicationTrafficAndNetworkTime();
  SchemaMismatchNeverEnablesApplicationTraffic();
  HeartbeatTimeoutClosesAndFreshTrafficReopensTheGate();
  assert(xrobot_test::AllocationCount() == allocations);
  return 0;
}
