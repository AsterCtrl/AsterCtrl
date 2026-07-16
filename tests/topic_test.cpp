#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/runtime/cooperative_executor.hpp"
#include "aster/runtime/module_context.hpp"
#include "aster/runtime/port_registry.hpp"
#include "aster/runtime/topic.hpp"

namespace test {

struct Command {
  std::int16_t target{};
};

}  // namespace test

namespace aster::runtime {

template <>
struct TypeSupport<test::Command> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.msg.Command",
            SchemaHash{{std::byte{0x10}, std::byte{0x20}, std::byte{0x30},
                        std::byte{0x40}}},
            2};
  }

  static Status Encode(const test::Command& message,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.size() < 2) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    const auto value = static_cast<std::uint16_t>(message.target);
    output[0] = static_cast<std::byte>(value & 0xffU);
    output[1] = static_cast<std::byte>((value >> 8U) & 0xffU);
    written = 2;
    return Status::kOk;
  }

  static Status Decode(std::span<const std::byte> input,
                       test::Command& message) noexcept {
    if (input.size() != 2) {
      return Status::kInvalidArgument;
    }
    const auto value = static_cast<std::uint16_t>(input[0]) |
                       (static_cast<std::uint16_t>(input[1]) << 8U);
    message.target = static_cast<std::int16_t>(value);
    return Status::kOk;
  }
};

}  // namespace aster::runtime

namespace {

using aster::runtime::CooperativeExecutor;
using aster::runtime::DeliveryPolicy;
using aster::runtime::ExecutionContext;
using aster::runtime::ExecutionKind;
using aster::runtime::MessageInfo;
using aster::runtime::ModuleContext;
using aster::runtime::ModuleServices;
using aster::runtime::StaticPortRegistry;
using aster::runtime::StaticTopic;
using aster::runtime::StaticTopicChannel;
using aster::runtime::Status;
using aster::runtime::TopicPublisher;
using aster::runtime::TopicSubscription;

struct Recorder {
  std::array<std::int16_t, 4> values{};
  std::array<std::uint32_t, 4> sequences{};
  std::size_t size{};
  ExecutionKind execution_kind{ExecutionKind::kInterrupt};
};

struct CongestedRecorder {
  Recorder recorder;
  aster::runtime::Executor* executor{};
  TopicPublisher<test::Command> publisher;
  bool reentered{};
};

void NoOp(void*, const ExecutionContext&) noexcept {}

void Record(void* state, const test::Command& message, const MessageInfo& info,
            const ExecutionContext& context) noexcept {
  auto& recorder = *static_cast<Recorder*>(state);
  recorder.values[recorder.size] = message.target;
  recorder.sequences[recorder.size] = info.sequence;
  ++recorder.size;
  recorder.execution_kind = context.kind();
}

void RecordAndFillExecutor(void* state, const test::Command& message,
                           const MessageInfo& info,
                           const ExecutionContext& context) noexcept {
  auto& congested = *static_cast<CongestedRecorder*>(state);
  Record(&congested.recorder, message, info, context);
  if (!congested.reentered) {
    congested.reentered = true;
    assert(congested.publisher.Publish(test::Command{20}, 2'000, context) ==
           Status::kOk);
  }
  assert(congested.executor->TryPost({NoOp, nullptr}, context) == Status::kOk);
}

void GeneratedTypeSupportDefinesTheWireContract() {
  constexpr auto descriptor =
      aster::runtime::TypeSupport<test::Command>::descriptor();
  static_assert(aster::runtime::MessageType<test::Command>);
  static_assert(descriptor.max_serialized_size == 2);
  assert(descriptor.name == "test.msg.Command");

  std::array<std::byte, 2> bytes{};
  std::size_t written{};
  assert(aster::runtime::TypeSupport<test::Command>::Encode(
             test::Command{-300}, bytes, written) == Status::kOk);
  assert(written == 2);
  test::Command decoded;
  assert(aster::runtime::TypeSupport<test::Command>::Decode(bytes, decoded) ==
         Status::kOk);
  assert(decoded.target == -300);
}

void LatestDeliveryCoalescesWithoutInlineCallbacks() {
  CooperativeExecutor<2> executor("control", 5);
  Recorder recorder;
  TopicSubscription<test::Command, 1> subscription(
      executor, DeliveryPolicy::kLatest, Record, &recorder);
  StaticTopic<test::Command, 1> topic("system/command");
  const ExecutionContext publisher("input", ExecutionKind::kThread, 4);

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(topic.Connect(subscription) == Status::kOk);
  assert(topic.Seal() == Status::kOk);
  const auto output = topic.publisher();

  assert(output.Publish(test::Command{10}, 1'000, publisher) == Status::kOk);
  assert(output.Publish(test::Command{20}, 2'000, publisher) == Status::kOk);
  assert(recorder.size == 0);
  assert(executor.pending() == 1);

  assert(executor.RunOne() == Status::kOk);
  assert(recorder.size == 1);
  assert(recorder.values[0] == 20);
  assert(recorder.sequences[0] == 2);
  assert(recorder.execution_kind == ExecutionKind::kThread);
  assert(subscription.stats().overwritten == 1);
}

void KeepAllDeliveryAppliesBoundedBackpressure() {
  CooperativeExecutor<2> executor("control", 5);
  Recorder recorder;
  TopicSubscription<test::Command, 2> subscription(
      executor, DeliveryPolicy::kKeepAll, Record, &recorder);
  StaticTopic<test::Command, 1> topic("system/events");
  const ExecutionContext publisher("input", ExecutionKind::kThread, 4);

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(topic.Connect(subscription) == Status::kOk);
  assert(topic.Seal() == Status::kOk);

  assert(topic.publisher().Publish(test::Command{10}, 1'000, publisher) ==
         Status::kOk);
  assert(topic.publisher().Publish(test::Command{20}, 2'000, publisher) ==
         Status::kOk);
  assert(topic.publisher().Publish(test::Command{30}, 3'000, publisher) ==
         Status::kCapacityExceeded);

  assert(executor.RunOne() == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(recorder.size == 2);
  assert(recorder.values[0] == 10);
  assert(recorder.values[1] == 20);
  assert(subscription.stats().dropped == 1);
}

void TopicMustBeSealedBeforePublishing() {
  CooperativeExecutor<1> executor("control", 5);
  Recorder recorder;
  TopicSubscription<test::Command, 1> subscription(
      executor, DeliveryPolicy::kLatest, Record, &recorder);
  StaticTopic<test::Command, 1> topic("system/command");
  const ExecutionContext publisher("input", ExecutionKind::kThread, 4);

  assert(topic.publisher().Publish(test::Command{10}, 1'000, publisher) ==
         Status::kInvalidState);
  assert(topic.Seal() == Status::kOk);
  assert(topic.Connect(subscription) == Status::kInvalidState);
}

void SchedulingFailureCannotLeaveSilentlyStuckLatestData() {
  CooperativeExecutor<1> executor("control", 5);
  CongestedRecorder recorder{{}, &executor, {}, false};
  TopicSubscription<test::Command, 1> subscription(
      executor, DeliveryPolicy::kLatest, RecordAndFillExecutor, &recorder);
  StaticTopic<test::Command, 1> topic("system/command");
  const ExecutionContext publisher("input", ExecutionKind::kThread, 4);

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(topic.Connect(subscription) == Status::kOk);
  assert(topic.Seal() == Status::kOk);
  recorder.publisher = topic.publisher();
  assert(topic.publisher().Publish(test::Command{10}, 1'000, publisher) ==
         Status::kOk);

  assert(executor.RunOne() == Status::kOk);
  assert(subscription.pending() == 1);
  assert(topic.publisher().Publish(test::Command{30}, 3'000, publisher) ==
         Status::kCapacityExceeded);
  assert(subscription.pending() == 0);
}

void StaticChannelFansOutThroughModuleContexts() {
  CooperativeExecutor<2> first_executor("first", 5);
  CooperativeExecutor<2> second_executor("second", 4);
  StaticTopicChannel<test::Command, 2, 1> channel(
      "system/state", DeliveryPolicy::kLatest);
  StaticPortRegistry<2> ports;
  Recorder first;
  Recorder second;
  const ExecutionContext caller("publisher", ExecutionKind::kThread, 6);

  assert(first_executor.Initialize() == Status::kOk);
  assert(second_executor.Initialize() == Status::kOk);
  assert(first_executor.Start() == Status::kOk);
  assert(second_executor.Start() == Status::kOk);
  assert(ports.AddTopicPublisher("system/state", channel) == Status::kOk);
  assert(ports.AddTopicSubscriber("system/state", channel) == Status::kOk);
  assert(ports.Seal() == Status::kOk);

  ModuleContext first_context(
      "node", "first_module",
      ModuleServices{.executor = &first_executor, .ports = &ports});
  ModuleContext second_context(
      "node", "second_module",
      ModuleServices{.executor = &second_executor, .ports = &ports});
  aster::runtime::TopicSubscriber<test::Command> first_subscriber;
  aster::runtime::TopicSubscriber<test::Command> second_subscriber;
  TopicPublisher<test::Command> publisher;

  assert(first_context.ResolveTopicSubscriber("system/state", first_subscriber) ==
         Status::kOk);
  assert(second_context.ResolveTopicSubscriber("system/state",
                                               second_subscriber) == Status::kOk);
  assert(first_subscriber.Bind(Record, &first) == Status::kOk);
  assert(second_subscriber.Bind(Record, &second) == Status::kOk);
  assert(first_context.ResolveTopicPublisher("system/state", publisher) ==
         Status::kOk);
  assert(channel.Seal() == Status::kOk);

  assert(publisher.Publish(test::Command{42}, 4'200, caller) == Status::kOk);
  assert(first.size == 0);
  assert(second.size == 0);
  assert(first_executor.RunOne() == Status::kOk);
  assert(second_executor.RunOne() == Status::kOk);
  assert(first.size == 1);
  assert(second.size == 1);
  assert(first.values[0] == 42);
  assert(second.values[0] == 42);
  assert(first.execution_kind == ExecutionKind::kThread);
  assert(second.execution_kind == ExecutionKind::kThread);
}

}  // namespace

int main() {
  GeneratedTypeSupportDefinesTheWireContract();
  LatestDeliveryCoalescesWithoutInlineCallbacks();
  KeepAllDeliveryAppliesBoundedBackpressure();
  TopicMustBeSealedBeforePublishing();
  SchedulingFailureCannotLeaveSilentlyStuckLatestData();
  StaticChannelFansOutThroughModuleContexts();
  return 0;
}
