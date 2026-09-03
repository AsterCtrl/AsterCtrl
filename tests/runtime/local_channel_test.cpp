#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

#include "aster/channel.hpp"
#include "test_types.hpp"

namespace {

struct Observation {
  std::uint32_t value{};
  std::uint32_t sequence{};
  std::uint64_t timestamp_ns{};
};

aster::Status Observe(void* state, const test::Sample& sample, const aster::MessageInfo& info,
                      const aster::ExecutionContext&) noexcept {
  auto& observation = *static_cast<Observation*>(state);
  observation = {sample.value, info.sequence, info.source_timestamp_ns};
  return aster::Status::kOk;
}

aster::Status Count(void* state, const test::Sample&, const aster::MessageInfo&,
                    const aster::ExecutionContext&) noexcept {
  static_cast<std::atomic<std::uint32_t>*>(state)->fetch_add(1, std::memory_order_relaxed);
  return aster::Status::kOk;
}

}  // namespace

int main() {
  aster::LocalChannel<2, 2, 16> channel;
  aster::Publisher<test::Sample> publisher;
  aster::Subscriber<test::Sample> subscriber;
  Observation observation;

  assert(publisher.Bind(aster::ChannelRef(channel), "state/sample") == aster::Status::kOk);
  assert(subscriber.Bind(aster::ChannelRef(channel), "state/sample", Observe, &observation) ==
         aster::Status::kOk);
  assert(channel.Seal() == aster::Status::kOk);
  assert(channel.sealed());
  assert(channel.topic_count() == 1);

  const aster::ExecutionContext execution("test", aster::ExecutionKind::kThread, 200);
  assert(publisher.Publish(test::Sample{42}, 100, execution) == aster::Status::kOk);
  assert(observation.value == 42);
  assert(observation.sequence == 1);
  assert(observation.timestamp_ns == 100);
  assert(channel.stats().publications == 1);
  assert(channel.stats().deliveries == 1);

  aster::LocalChannel<1, 1, 16> concurrent_channel;
  aster::Publisher<test::Sample> concurrent_publisher;
  aster::Subscriber<test::Sample> concurrent_subscriber;
  std::atomic<std::uint32_t> received{};
  assert(concurrent_publisher.Bind(aster::ChannelRef(concurrent_channel), "concurrent") ==
         aster::Status::kOk);
  assert(concurrent_subscriber.Bind(aster::ChannelRef(concurrent_channel), "concurrent", Count,
                                    &received) == aster::Status::kOk);
  assert(concurrent_channel.Seal() == aster::Status::kOk);
  std::array<std::thread, 4> publishers;
  for (auto& thread : publishers) {
    thread = std::thread([&] {
      for (std::uint32_t value = 0; value < 1000; ++value) {
        assert(concurrent_publisher.Publish(test::Sample{value}, value, execution) ==
               aster::Status::kOk);
      }
    });
  }
  for (auto& thread : publishers) {
    thread.join();
  }
  assert(received.load(std::memory_order_relaxed) == 4000);
  assert(concurrent_channel.stats().publications == 4000);
  assert(concurrent_channel.stats().deliveries == 4000);

  aster::LocalChannel<1, 1, 4> missing_publisher;
  aster::Subscriber<test::Sample> orphan;
  assert(orphan.Bind(aster::ChannelRef(missing_publisher), "orphan", Observe, &observation) ==
         aster::Status::kOk);
  assert(missing_publisher.Seal() == aster::Status::kUnavailable);

  aster::LocalChannel<1, 1, 4> capacity;
  aster::Publisher<test::Sample> first;
  aster::Publisher<test::Sample> second;
  assert(first.Bind(aster::ChannelRef(capacity), "first") == aster::Status::kOk);
  assert(second.Bind(aster::ChannelRef(capacity), "second") == aster::Status::kCapacityExceeded);
}
