#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/runtime/mapped_resolver.hpp"
#include "aster/runtime/module_context.hpp"
#include "aster/runtime/topic.hpp"

namespace test {

struct Command {
  std::uint8_t value{};
};

class Device {
 public:
  static constexpr std::string_view TypeName() noexcept {
    return "test.hardware.Device/v1";
  }
};

class WrongDevice {
 public:
  static constexpr std::string_view TypeName() noexcept {
    return "test.hardware.Wrong/v1";
  }
};

}  // namespace test

namespace aster::runtime {

template <>
struct TypeSupport<test::Command> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.msg.MappedCommand", SchemaHash{{std::byte{0x72}}}, 1};
  }

  static Status Encode(const test::Command& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.empty()) return Status::kCapacityExceeded;
    output[0] = static_cast<std::byte>(value.value);
    written = 1;
    return Status::kOk;
  }

  static Status Decode(std::span<const std::byte> input,
                       test::Command& value) noexcept {
    if (input.size() != 1U) return Status::kInvalidArgument;
    value.value = std::to_integer<std::uint8_t>(input[0]);
    return Status::kOk;
  }
};

}  // namespace aster::runtime

namespace {

using aster::runtime::MappedHardwareResolver;
using aster::runtime::MappedPortResolver;
using aster::runtime::NameMapping;
using aster::runtime::StaticHardwareRegistry;
using aster::runtime::StaticPortRegistry;
using aster::runtime::StaticTopic;
using aster::runtime::Status;
using aster::runtime::TopicPublisher;
using aster::runtime::TopicSubscriber;

void MapsLocalNamesWithoutWeakeningTypeChecks() {
  StaticTopic<test::Command, 1> topic("/control/command");
  StaticPortRegistry<1> global_ports;
  StaticHardwareRegistry<1> global_hardware;
  test::Device device;
  const std::array port_mappings{
      NameMapping{"command_out", "/control/command"}};
  const std::array hardware_mappings{NameMapping{"motor", "yaw_motor"}};
  MappedPortResolver ports(port_mappings);
  MappedHardwareResolver hardware(hardware_mappings);
  TopicPublisher<test::Command> publisher;
  TopicSubscriber<test::Command> wrong_port;
  void* resolved{};

  assert(topic.Seal() == Status::kOk);
  assert(global_ports.AddTopicPublisher("/control/command", topic) ==
         Status::kOk);
  assert(global_ports.Seal() == Status::kOk);
  assert(global_hardware.Add("yaw_motor", device) == Status::kOk);
  assert(global_hardware.Seal() == Status::kOk);

  assert(ports.Resolve("command_out",
                       aster::runtime::PortKind::kTopicPublisher,
                       aster::runtime::TypeSupport<test::Command>::descriptor()
                           .schema_hash,
                       resolved) == Status::kInvalidState);
  assert(ports.Bind(global_ports) == Status::kOk);
  assert(hardware.Bind(global_hardware) == Status::kOk);
  assert(ports.bound());
  assert(hardware.bound());

  aster::runtime::ModuleContext context(
      "node", "controller",
      {.ports = &ports, .hardware = &hardware});
  test::Device* resolved_device{};
  test::WrongDevice* wrong_device{};
  assert(context.ResolveTopicPublisher("command_out", publisher) ==
         Status::kOk);
  assert(context.ResolveTopicSubscriber("command_out", wrong_port) ==
         Status::kTypeMismatch);
  assert(context.ResolveTopicPublisher("missing", publisher) ==
         Status::kUnavailable);
  assert(context.ResolveHardware("motor", resolved_device) == Status::kOk);
  assert(resolved_device == &device);
  assert(context.ResolveHardware("motor", wrong_device) ==
         Status::kTypeMismatch);
  assert(wrong_device == nullptr);
  assert(ports.Bind(global_ports) == Status::kInvalidState);
  assert(hardware.Bind(global_hardware) == Status::kInvalidState);
}

void RejectsInvalidMappingTables() {
  StaticPortRegistry<1> upstream_ports;
  StaticHardwareRegistry<1> upstream_hardware;
  const std::array duplicate{
      NameMapping{"local", "first"}, NameMapping{"local", "second"}};
  const std::array empty{NameMapping{"", "global"}};
  MappedPortResolver duplicate_ports(duplicate);
  MappedHardwareResolver empty_hardware(empty);
  MappedPortResolver self_ports({});
  MappedHardwareResolver self_hardware({});

  assert(duplicate_ports.Bind(upstream_ports) == Status::kInvalidArgument);
  assert(empty_hardware.Bind(upstream_hardware) == Status::kInvalidArgument);
  assert(self_ports.Bind(self_ports) == Status::kInvalidArgument);
  assert(self_hardware.Bind(self_hardware) == Status::kInvalidArgument);
}

}  // namespace

int main() {
  MapsLocalNamesWithoutWeakeningTypeChecks();
  RejectsInvalidMappingTables();
  return 0;
}
