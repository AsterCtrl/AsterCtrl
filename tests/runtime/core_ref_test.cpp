#include "aster/core_ref.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace {

class TestConfigurator final : public aster::Configurator {
 public:
  aster::Status Get(std::string_view key, std::span<std::byte> output,
                    std::size_t& written) const noexcept override {
    written = 0;
    if (key != "answer" || output.size() < sizeof(value)) {
      return aster::Status::kNotFound;
    }
    std::memcpy(output.data(), &value, sizeof(value));
    written = sizeof(value);
    return aster::Status::kOk;
  }

  std::uint32_t value{42};
};

class TestLogger final : public aster::Logger {
 public:
  aster::Status Write(aster::LogLevel level, std::string_view message,
                      const aster::ExecutionContext&) noexcept override {
    last_level = level;
    last_message = message;
    return aster::Status::kOk;
  }

  aster::LogLevel last_level{aster::LogLevel::kTrace};
  std::string_view last_message;
};

class TestExecutor final : public aster::Executor {
 public:
  std::string_view Name() const noexcept override { return "test"; }
  aster::Status TryPost(aster::WorkItem work,
                        const aster::ExecutionContext& caller) noexcept override {
    work.Run(caller);
    return aster::Status::kOk;
  }
  aster::Status TryPostAt(std::uint64_t, aster::WorkItem work,
                          const aster::ExecutionContext& caller) noexcept override {
    return TryPost(work, caller);
  }
};

class TestParameters final : public aster::ParameterStore {
 public:
  aster::Status Get(std::string_view, std::string_view, std::span<std::byte> output,
                    std::size_t& written) const noexcept override {
    written = 0;
    if (output.empty()) {
      return aster::Status::kCapacityExceeded;
    }
    output[0] = value;
    written = 1;
    return aster::Status::kOk;
  }
  aster::Status Set(std::string_view, std::string_view, std::span<const std::byte> input,
                    const aster::ExecutionContext&) noexcept override {
    if (input.size() != 1) {
      return aster::Status::kTypeMismatch;
    }
    value = input[0];
    return aster::Status::kOk;
  }

  std::byte value{};
};

class TestClock final : public aster::Clock {
 public:
  aster::ClockDomain domain() const noexcept override { return aster::ClockDomain::kSimulated; }
  std::uint64_t NowNs() const noexcept override { return 123; }
};

class TestAllocator final : public aster::Allocator {
 public:
  void* Allocate(std::size_t size, std::size_t) noexcept override {
    return size <= storage.size() ? storage.data() : nullptr;
  }
  void Deallocate(void*, std::size_t, std::size_t) noexcept override {}

  std::array<std::byte, 16> storage{};
};

struct Device {
  static constexpr std::string_view TypeName() noexcept { return "test.Device"; }
};

class TestHardware final : public aster::HardwareManager {
 public:
  aster::Status Resolve(std::string_view name, std::string_view type,
                        void*& resolved) noexcept override {
    if (name != "device" || type != Device::TypeName()) {
      return aster::Status::kNotFound;
    }
    resolved = &device;
    return aster::Status::kOk;
  }

  Device device;
};

void Increment(void* state, const aster::ExecutionContext&) noexcept {
  ++*static_cast<int*>(state);
}

}  // namespace

int main() {
  TestConfigurator configurator;
  TestLogger logger;
  TestExecutor executor;
  TestParameters parameters;
  TestClock clock;
  TestAllocator allocator;
  TestHardware hardware;
  aster::LocalChannel<1, 1, 8> channel;
  aster::LocalRpc<1, 8, 8> rpc{aster::ExecutorRef(executor)};
  const aster::CoreRef core({
      .configurator = aster::ConfiguratorRef(configurator),
      .logger = aster::LoggerRef(logger),
      .executor = aster::ExecutorRef(executor),
      .channel = aster::ChannelRef(channel),
      .rpc = aster::RpcRef(rpc),
      .parameter = aster::ParameterRef(parameters),
      .clock = aster::ClockRef(clock),
      .allocator = aster::AllocatorRef(allocator),
      .hardware = aster::HardwareManagerRef(hardware),
  });

  std::uint32_t answer{};
  assert(core.configurator().Get("answer", answer) == aster::Status::kOk);
  assert(answer == 42);
  const aster::ExecutionContext context("caller", aster::ExecutionKind::kThread, 1);
  assert(core.logger().Write(aster::LogLevel::kInfo, "ready", context) == aster::Status::kOk);
  assert(logger.last_message == "ready");
  int count{};
  assert(core.executor().TryPost({Increment, &count}, context) == aster::Status::kOk);
  assert(count == 1);
  assert(core.clock().domain() == aster::ClockDomain::kSimulated);
  assert(core.clock().NowNs() == 123);
  assert(core.allocator().Allocate(8, 8) != nullptr);
  Device* device{};
  assert(core.hardware().Resolve("device", device) == aster::Status::kOk);
  assert(device == &hardware.device);

  std::array<std::byte, 1> value{std::byte{0x2a}};
  assert(core.parameter().Set("gain", "uint8", value, context) == aster::Status::kOk);
  std::array<std::byte, 1> output{};
  std::size_t written{};
  assert(core.parameter().Get("gain", "uint8", output, written) == aster::Status::kOk);
  assert(written == 1 && output[0] == value[0]);

  const aster::CoreRef empty;
  assert(empty.logger().Write(aster::LogLevel::kInfo, "ignored", context) ==
         aster::Status::kUnavailable);
}
