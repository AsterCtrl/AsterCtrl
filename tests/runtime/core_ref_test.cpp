#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "aster_runtime/core/allocator.hpp"
#include "aster_runtime/core/clock.hpp"
#include "aster_runtime/core/configurator.hpp"
#include "aster_runtime/core/executor.hpp"
#include "aster_runtime/core/hardware.hpp"
#include "aster_runtime/core/logger.hpp"
#include "aster_runtime/core/parameter.hpp"
#include "aster_runtime/core_adapter.hpp"
#include "aster_runtime/local_channel.hpp"
#include "aster_runtime/local_rpc.hpp"

namespace {

class TestConfigurator final : public aster::Configurator {
 public:
  aster::Status Get(std::string_view key, aster::ValueView& output) const noexcept override {
    if (key != "answer") return aster::Status::kNotFound;
    output = aster::ValueView(value);
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
  aster::Status Get(std::string_view, aster::ValueView& output,
                    std::span<std::byte>) const noexcept override {
    output = aster::ValueView(value);
    return aster::Status::kOk;
  }
  aster::Status Set(std::string_view, aster::ValueView input,
                    const aster::ExecutionContext&) noexcept override {
    return input.Get(value);
  }

  std::uint8_t value{};
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
  static_assert(sizeof(aster::LoggerRef) == sizeof(void*));
  static_assert(sizeof(aster::ExecutorRef) == sizeof(void*));
  static_assert(sizeof(aster::ChannelRef) == sizeof(void*));
  static_assert(sizeof(aster::RpcRef) == sizeof(void*));
  static_assert(sizeof(aster::ConfiguratorRef) == sizeof(void*));
  static_assert(sizeof(aster::ParameterRef) == sizeof(void*));
  static_assert(sizeof(aster::ClockRef) == sizeof(void*));
  static_assert(sizeof(aster::AllocatorRef) == sizeof(void*));
  static_assert(sizeof(aster::CoreRefOverlay) <= 160);

  TestConfigurator configurator;
  TestLogger logger;
  TestExecutor executor;
  TestParameters parameters;
  TestClock clock;
  TestAllocator allocator;
  TestHardware hardware;
  aster::LocalChannel<1, 1, 8> channel;
  aster::LocalRpc<1, 8, 8> rpc{aster::ExecutorRef(executor)};
  const aster::CoreAdapter core_adapter({
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
  const auto core = core_adapter.ref();

  std::uint32_t answer{};
  assert(core.configurator().Get("answer", answer) == aster::Status::kOk);
  assert(answer == 42);
  const aster::ExecutionContext context("caller", aster::ExecutionKind::kThread, 1);

  TestConfigurator instance_configurator;
  instance_configurator.value = 7;
  const aster::CoreRefOverlay overlay(core, instance_configurator);
  answer = 0;
  assert(overlay.ref().configurator().Get("answer", answer) == aster::Status::kOk);
  assert(answer == 7);
  assert(overlay.ref().channel());
  std::uint64_t now = 99;
  assert(overlay.ref().clock().NowNs(now) == aster::Status::kOk && now == 123);
  assert(aster::ClockRef{}.NowNs(now) == aster::Status::kUnavailable && now == 123);
  const aster_clock_base_t failed_clock{sizeof(aster_clock_base_t), nullptr, nullptr,
                                        [](void*, std::uint64_t* output) -> aster_status_t {
                                          *output = 0;
                                          return ASTER_STATUS_UNAVAILABLE;
                                        }};
  assert(aster::ClockRef(&failed_clock).NowNs(now) == aster::Status::kUnavailable && now == 123);

  const auto* logger_service = core.NativeHandle()->logger(core.NativeHandle()->impl);
  assert(logger_service != nullptr && logger_service->struct_size == sizeof(*logger_service));
  aster_execution_context_t malformed_context{};
  assert(logger_service->write(logger_service->impl, ASTER_LOG_LEVEL_INFO, {"invalid", 7},
                               &malformed_context) == ASTER_STATUS_INVALID_ARGUMENT);

  assert(core.logger().Write(aster::LogLevel::kInfo, "ready", context) == aster::Status::kOk);
  assert(logger.last_message == "ready");
  int count{};
  assert(core.executor().TryPost(aster::WorkItem::Bind<Increment>(&count), context) ==
         aster::Status::kOk);
  assert(count == 1);
  aster::ClockDomain domain{};
  assert(core.clock().GetDomain(domain) == aster::Status::kOk &&
         domain == aster::ClockDomain::kSimulated);
  assert(core.clock().NowNs(now) == aster::Status::kOk && now == 123);
  assert(core.allocator().Allocate(8, 8) != nullptr);
  Device* device{};
  assert(core.hardware().Resolve("device", device) == aster::Status::kOk);
  assert(device == &hardware.device);

  assert(core.parameter().Set("gain", std::uint8_t{42}, context) == aster::Status::kOk);
  std::uint8_t output{};
  assert(core.parameter().Get("gain", output) == aster::Status::kOk);
  assert(output == 42);

  const aster::CoreRef empty;
  assert(empty.logger().Write(aster::LogLevel::kInfo, "ignored", context) ==
         aster::Status::kUnavailable);

  const aster::CoreAdapter empty_adapter({});
  const auto* allocator_service =
      empty_adapter.NativeHandle()->allocator(empty_adapter.NativeHandle()->impl);
  assert(allocator_service == nullptr);
}
