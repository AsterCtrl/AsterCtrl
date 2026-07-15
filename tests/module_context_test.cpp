#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "xrobot/runtime/cooperative_executor.hpp"
#include "xrobot/runtime/hardware_registry.hpp"
#include "xrobot/runtime/module_context.hpp"
#include "xrobot/runtime/parameter_registry.hpp"
#include "xrobot/runtime/port_registry.hpp"

namespace test {

struct Command {
  std::uint8_t value{};
};

}  // namespace test

namespace xrobot::runtime {

template <>
struct TypeSupport<test::Command> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.msg.ContextCommand", SchemaHash{{std::byte{0xc1}}}, 1};
  }
  static Status Encode(const test::Command& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.empty()) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(value.value);
    written = 1;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::Command& value) noexcept {
    if (input.size() != 1) {
      return Status::kInvalidArgument;
    }
    value.value = static_cast<std::uint8_t>(input[0]);
    return Status::kOk;
  }
};

}  // namespace xrobot::runtime

namespace {

using xrobot::runtime::CooperativeExecutor;
using xrobot::runtime::DeliveryPolicy;
using xrobot::runtime::DiagnosticRecord;
using xrobot::runtime::DiagnosticSeverity;
using xrobot::runtime::DiagnosticSink;
using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::LogLevel;
using xrobot::runtime::LogRecord;
using xrobot::runtime::LogSink;
using xrobot::runtime::MessageInfo;
using xrobot::runtime::ModuleContext;
using xrobot::runtime::ModuleServices;
using xrobot::runtime::Parameter;
using xrobot::runtime::ParameterDescriptor;
using xrobot::runtime::ParameterMutability;
using xrobot::runtime::ParameterPersistence;
using xrobot::runtime::StaticParameterRegistry;
using xrobot::runtime::StaticHardwareRegistry;
using xrobot::runtime::StaticPortRegistry;
using xrobot::runtime::StaticTopic;
using xrobot::runtime::Status;
using xrobot::runtime::SteadyClock;
using xrobot::runtime::TopicPublisher;
using xrobot::runtime::TopicSubscriber;
using xrobot::runtime::TopicSubscription;

class FakeClock final : public SteadyClock {
 public:
  std::uint64_t NowNs() const noexcept override { return 42'000; }
};

class RecordingLog final : public LogSink {
 public:
  Status Write(const LogRecord& record,
               const ExecutionContext&) noexcept override {
    last = record;
    ++count;
    return Status::kOk;
  }

  LogRecord last{};
  int count{};
};

class RecordingDiagnostics final : public DiagnosticSink {
 public:
  Status Report(const DiagnosticRecord& record,
                const ExecutionContext&) noexcept override {
    last = record;
    ++count;
    return Status::kOk;
  }

  DiagnosticRecord last{};
  int count{};
};

class TestHardware {
 public:
  static constexpr std::string_view TypeName() noexcept {
    return "test.hardware.Device/v1";
  }

  int value{};
};

class OtherHardware {
 public:
  static constexpr std::string_view TypeName() noexcept {
    return "test.hardware.Other/v1";
  }
};

struct Receiver {
  std::uint8_t value{};
};

void Receive(void* state, const test::Command& command, const MessageInfo&,
             const ExecutionContext&) noexcept {
  static_cast<Receiver*>(state)->value = command.value;
}

void ContextResolvesGeneratedResourcesWithoutOwningThem() {
  CooperativeExecutor<2> executor("control", 4);
  StaticTopic<test::Command, 1> topic("robot/command");
  TopicSubscription<test::Command, 1> subscription(
      executor, DeliveryPolicy::kLatest);
  StaticPortRegistry<2> ports;
  constexpr ParameterDescriptor<float> gain_descriptor{
      "gain", "N/A", 1.0F, 0.0F, 10.0F,
      ParameterMutability::kRuntime, ParameterPersistence::kVolatile};
  Parameter gain(gain_descriptor);
  StaticParameterRegistry<1> parameters;
  StaticHardwareRegistry<1> hardware;
  TestHardware device{7};
  FakeClock clock;
  RecordingLog log;
  RecordingDiagnostics diagnostics;
  Receiver receiver;
  const ExecutionContext caller("main", ExecutionKind::kThread, 1);

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(topic.Connect(subscription) == Status::kOk);
  assert(topic.Seal() == Status::kOk);
  assert(ports.AddTopicPublisher("command_out", topic) == Status::kOk);
  assert(ports.AddTopicSubscriber("command_in", subscription) == Status::kOk);
  assert(ports.Seal() == Status::kOk);
  assert(parameters.Add(gain) == Status::kOk);
  assert(parameters.Seal() == Status::kOk);
  assert(hardware.Add("drive", device) == Status::kOk);
  assert(hardware.Seal() == Status::kOk);

  ModuleServices services{.executor = &executor,
                          .clock = &clock,
                          .log = &log,
                          .diagnostics = &diagnostics,
                          .ports = &ports,
                          .parameters = &parameters,
                          .hardware = &hardware};
  ModuleContext context("node", "controller", services);
  TopicPublisher<test::Command> publisher;
  TopicSubscriber<test::Command> subscriber;
  Parameter<float>* resolved_gain{};
  TestHardware* resolved_device{};
  OtherHardware* wrong_device{};

  assert(context.ResolveTopicPublisher("command_out", publisher) ==
         Status::kOk);
  assert(context.ResolveTopicSubscriber("command_in", subscriber) ==
         Status::kOk);
  assert(subscriber.Bind(Receive, &receiver) == Status::kOk);
  assert(context.ResolveParameter("gain", resolved_gain) == Status::kOk);
  assert(resolved_gain == &gain);
  assert(context.ResolveHardware("drive", resolved_device) == Status::kOk);
  assert(resolved_device == &device);
  assert(resolved_device->value == 7);
  assert(context.ResolveHardware("drive", wrong_device) ==
         Status::kTypeMismatch);
  assert(wrong_device == nullptr);
  assert(publisher.Publish({7}, context.NowNs(), caller) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(receiver.value == 7);

  assert(context.Log(LogLevel::kInfo, "ready", caller) == Status::kOk);
  assert(log.count == 1);
  assert(log.last.node_name == "node");
  assert(log.last.module_name == "controller");
  assert(log.last.timestamp_ns == 42'000);
  assert(context.Report("control.ready", DiagnosticSeverity::kInfo, 1,
                        caller) == Status::kOk);
  assert(diagnostics.count == 1);
  assert(diagnostics.last.name == "control.ready");
  assert(diagnostics.last.timestamp_ns == 42'000);
}

void MissingCapabilitiesReturnUnavailable() {
  ModuleContext context("node", "minimal");
  TopicPublisher<test::Command> publisher;
  Parameter<float>* parameter{};
  TestHardware* hardware{};
  const ExecutionContext caller("main", ExecutionKind::kThread, 1);

  assert(context.NowNs() == 0);
  assert(context.ResolveTopicPublisher("missing", publisher) ==
         Status::kUnavailable);
  assert(context.ResolveParameter("missing", parameter) ==
         Status::kUnavailable);
  assert(context.ResolveHardware("missing", hardware) ==
         Status::kUnavailable);
  assert(context.Log(LogLevel::kInfo, "ignored", caller) ==
         Status::kUnavailable);
}

}  // namespace

int main() {
  ContextResolvesGeneratedResourcesWithoutOwningThem();
  MissingCapabilitiesReturnUnavailable();
  return 0;
}
