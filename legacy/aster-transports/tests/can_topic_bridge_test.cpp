#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "allocation_tracker.hpp"
#include "xrobot/runtime/cooperative_executor.hpp"
#include "xrobot/runtime/topic.hpp"
#include "xrobot/transport/can/topic_bridge.hpp"

namespace test {

struct Command {
  std::array<std::uint16_t, 4> values{};
};

}  // namespace test

namespace xrobot::runtime {

template <>
struct TypeSupport<test::Command> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.msg.CanCommand", SchemaHash{{std::byte{0x42}}}, 8};
  }

  static Status Encode(const test::Command& message,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 8) {
      return Status::kCapacityExceeded;
    }
    for (const auto value : message.values) {
      output[written++] = static_cast<std::byte>(value & 0xffU);
      output[written++] = static_cast<std::byte>(value >> 8U);
    }
    return Status::kOk;
  }

  static Status Decode(std::span<const std::byte> input,
                       test::Command& message) noexcept {
    if (input.size() != 8) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < message.values.size(); ++index) {
      message.values[index] =
          static_cast<std::uint16_t>(input[index * 2]) |
          static_cast<std::uint16_t>(
              static_cast<std::uint16_t>(input[index * 2 + 1]) << 8U);
    }
    return Status::kOk;
  }
};

}  // namespace xrobot::runtime

namespace {

using xrobot::runtime::CooperativeExecutor;
using xrobot::runtime::DeliveryPolicy;
using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::MessageInfo;
using xrobot::runtime::StaticTopic;
using xrobot::runtime::Status;
using xrobot::runtime::TopicSubscription;
using namespace xrobot::transport::can;

struct Receiver {
  test::Command command;
  MessageInfo info;
  std::uint32_t count{};
};

void Receive(void* state, const test::Command& command, const MessageInfo& info,
             const ExecutionContext&) noexcept {
  auto& receiver = *static_cast<Receiver*>(state);
  receiver.command = command;
  receiver.info = info;
  ++receiver.count;
}

struct LoopbackBus {
  FastTopicIngress<test::Command>* ingress{};
  const ExecutionContext* receive_context{};
  std::uint64_t receive_time_ns{};
  std::uint32_t frame_index{};
  std::uint32_t drop_index{UINT32_MAX};
};

Status SendFrame(void* state, const CanFrame& frame,
                 const ExecutionContext&) noexcept {
  auto& bus = *static_cast<LoopbackBus*>(state);
  const auto index = bus.frame_index++;
  if (index != bus.drop_index) {
    bus.ingress->Accept(frame, bus.receive_time_ns, *bus.receive_context);
  }
  return Status::kOk;
}

void SameTopicContractCrossesTheSimulatedCanLink() {
  CooperativeExecutor<4> source_executor("source", 4);
  CooperativeExecutor<4> destination_executor("destination", 4);
  StaticTopic<test::Command, 1> source_topic("/control/command");
  StaticTopic<test::Command, 1> destination_topic("/control/command");
  Receiver receiver;
  TopicSubscription<test::Command, 1> destination_subscription(
      destination_executor, DeliveryPolicy::kLatest, Receive, &receiver);
  const ExecutionContext source_context("source", ExecutionKind::kThread, 4);
  const ExecutionContext destination_context("destination",
                                             ExecutionKind::kThread, 4);
  FastTopicIngress<test::Command> ingress(
      8, destination_topic.publisher(), {10'000'000, 30'000'000,
                                         RearmPolicy::kFreshSample});
  LoopbackBus bus{&ingress, &destination_context, 1'005'000'000};
  FastTopicEgress<test::Command> egress(
      8, CanPriority::kControl, {SendFrame, &bus});

  assert(source_executor.Initialize() == Status::kOk);
  assert(source_executor.Start() == Status::kOk);
  assert(destination_executor.Initialize() == Status::kOk);
  assert(destination_executor.Start() == Status::kOk);
  assert(destination_topic.Connect(destination_subscription) == Status::kOk);
  assert(destination_topic.Seal() == Status::kOk);
  assert(source_topic.Connect(egress) == Status::kOk);
  assert(source_topic.Seal() == Status::kOk);

  const test::Command command{{100, 200, 300, 400}};
  assert(source_topic.publisher().Publish(command, 1'000'000'000,
                                          source_context) == Status::kOk);
  assert(receiver.count == 0);
  assert(destination_executor.RunOne() == Status::kOk);
  assert(receiver.count == 1);
  assert(receiver.command.values == command.values);
  assert(receiver.info.source_timestamp_ns == 1'000'000'000);
  assert(ingress.freshness().state() == FreshnessState::kFresh);
}

void MissingFragmentDoesNotLeakAPartialMessageAndLatestRecovers() {
  CooperativeExecutor<4> destination_executor("destination", 4);
  StaticTopic<test::Command, 1> source_topic("/control/command");
  StaticTopic<test::Command, 1> destination_topic("/control/command");
  Receiver receiver;
  TopicSubscription<test::Command, 1> destination_subscription(
      destination_executor, DeliveryPolicy::kLatest, Receive, &receiver);
  const ExecutionContext source_context("source", ExecutionKind::kThread, 4);
  const ExecutionContext destination_context("destination",
                                             ExecutionKind::kThread, 4);
  FastTopicIngress<test::Command> ingress(
      8, destination_topic.publisher(), {10'000'000, 30'000'000,
                                         RearmPolicy::kFreshSample});
  LoopbackBus bus{&ingress, &destination_context, 2'005'000'000, 0, 1};
  FastTopicEgress<test::Command> egress(
      8, CanPriority::kControl, {SendFrame, &bus});

  assert(destination_executor.Initialize() == Status::kOk);
  assert(destination_executor.Start() == Status::kOk);
  assert(destination_topic.Connect(destination_subscription) == Status::kOk);
  assert(destination_topic.Seal() == Status::kOk);
  assert(source_topic.Connect(egress) == Status::kOk);
  assert(source_topic.Seal() == Status::kOk);
  assert(source_topic.publisher().Publish({{1, 2, 3, 4}}, 2'000'000'000,
                                          source_context) == Status::kOk);
  assert(destination_executor.pending() == 0);

  bus.drop_index = UINT32_MAX;
  bus.receive_time_ns = 2'015'000'000;
  assert(source_topic.publisher().Publish({{5, 6, 7, 8}}, 2'010'000'000,
                                          source_context) == Status::kOk);
  assert(destination_executor.RunOne() == Status::kOk);
  assert(receiver.count == 1);
  assert(receiver.command.values[0] == 5);
  assert(ingress.reassembly_stats().superseded == 1);
}

}  // namespace

int main() {
  const auto allocations = xrobot_test::AllocationCount();
  SameTopicContractCrossesTheSimulatedCanLink();
  MissingFragmentDoesNotLeakAPartialMessageAndLatestRecovers();
  assert(xrobot_test::AllocationCount() == allocations);
  return 0;
}
