#include "aster_runtime/platform/linux/plugin_loader.hpp"

#include <unistd.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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
#include "aster_runtime/platform/linux/supervisor.hpp"
#include "aster_runtime/runtime.hpp"

#ifndef ASTER_TEST_PLUGIN_PATH
#error ASTER_TEST_PLUGIN_PATH must be defined
#endif

#ifndef ASTER_INVALID_PLUGIN_PATH
#error ASTER_INVALID_PLUGIN_PATH must be defined
#endif

#ifndef ASTER_TEST_CPP_PACKAGE_PATH
#error ASTER_TEST_CPP_PACKAGE_PATH must be defined
#endif

namespace {

class TestConfigurator final : public aster::Configurator {
 public:
  aster::Status Get(std::string_view key, aster::ValueView& output) const noexcept override {
    if (key != "answer") return aster::Status::kNotFound;
    output = aster::ValueView(std::uint32_t{42});
    return aster::Status::kOk;
  }
};

class TestLogger final : public aster::Logger {
 public:
  aster::Status Write(aster::LogLevel level, std::string_view message,
                      const aster::ExecutionContext& caller) noexcept override {
    last_level = level;
    last_message.assign(message);
    last_executor.assign(caller.executor_name());
    last_timestamp = caller.timestamp_ns();
    return aster::Status::kOk;
  }

  aster::LogLevel last_level{aster::LogLevel::kTrace};
  std::string last_message;
  std::string last_executor;
  std::uint64_t last_timestamp{};
};

class TestClock final : public aster::Clock {
 public:
  aster::ClockDomain domain() const noexcept override { return aster::ClockDomain::kSimulated; }
  std::uint64_t NowNs() const noexcept override { return 123; }
};

class TestExecutor final : public aster::Executor {
 public:
  [[nodiscard]] std::string_view Name() const noexcept override { return "plugin-test"; }
  aster::Status TryPost(aster::WorkItem work,
                        const aster::ExecutionContext& caller) noexcept override {
    if (!work) {
      return aster::Status::kInvalidArgument;
    }
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
  aster::Status Get(std::string_view, aster::ValueView&,
                    std::span<std::byte>) const noexcept override {
    return aster::Status::kNotFound;
  }
  aster::Status Set(std::string_view, aster::ValueView,
                    const aster::ExecutionContext&) noexcept override {
    return aster::Status::kNotFound;
  }
};

class TestAllocator final : public aster::Allocator {
 public:
  void* Allocate(std::size_t size, std::size_t) noexcept override {
    return size <= storage.size() ? storage.data() : nullptr;
  }
  void Deallocate(void*, std::size_t, std::size_t) noexcept override {}

 private:
  std::array<std::byte, 32> storage{};
};

class TestHardware final : public aster::HardwareManager {
 public:
  aster::Status Resolve(std::string_view, std::string_view, void*& device) noexcept override {
    device = nullptr;
    return aster::Status::kNotFound;
  }
};

}  // namespace

int main() {
  const auto trace = std::filesystem::temp_directory_path() /
                     ("aster-plugin-loader-trace-" + std::to_string(::getpid()) + ".txt");
  std::error_code error;
  std::filesystem::remove(trace, error);
  assert(setenv("ASTER_TEST_PLUGIN_TRACE", trace.c_str(), 1) == 0);

  TestConfigurator configurator;
  TestLogger logger;
  TestClock clock;
  TestExecutor executor;
  TestParameters parameters;
  TestAllocator allocator;
  TestHardware hardware;
  aster::LocalChannel<1, 1, 1> channel;
  aster::LocalRpc<1, 4, 4, 8> rpc{aster::ExecutorRef(executor)};
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
  aster::platform::linux::PluginLoader loader;
  assert(loader.Open(ASTER_TEST_PLUGIN_PATH) == aster::Status::kOk);
  assert(loader.is_open());
  assert(loader.name() == "test-plugin");
  assert(loader.version() == "1.0.0");
  assert(loader.module_names().size() == 1);
  assert(loader.module_names()[0] == "test.Module");
  assert(loader.modules().empty());
  assert(loader.CreateModule("missing.Module", "missing", core) == aster::Status::kNotFound);
  assert(loader.CreateModule("test.Module", "loaded", core) == aster::Status::kOk);
  assert(loader.modules().size() == 1);
  assert(loader.modules()[0].module.Info().name == "loaded");

  std::array<aster::RegistrySlot, 2> registries{{{&channel}, {&rpc}}};
  aster::Runtime runtime(loader.modules(), registries);
  assert(runtime.Initialize() == aster::Status::kOk);
  assert(logger.last_level == aster::LogLevel::kInfo);
  assert(logger.last_message == "plugin initialized");
  assert(runtime.Start() == aster::Status::kOk);
  assert(channel.stats().publications == 1001);
  assert(channel.stats().deliveries == 1001);
  assert(rpc.stats().calls == 1001);
  assert(rpc.stats().completed == 1001);
  runtime.Shutdown();
  loader.Close();
  assert(!loader.is_open());

  std::ifstream input(trace);
  std::string events;
  input >> events;
  assert(events == "ITSB");

  aster::platform::linux::PluginLoader generated;
  assert(generated.Open(ASTER_TEST_CPP_PACKAGE_PATH) == aster::Status::kOk);
  assert(generated.name() == "generated-package");
  assert(generated.version() == "1.2.3");
  assert(generated.module_names().size() == 3);
  assert(generated.module_names()[0] == "test.GeneratedModule");
  assert(generated.CreateModule("test.GeneratedModule", "generated-a", core) == aster::Status::kOk);
  assert(generated.CreateModule("test.GeneratedModule", "generated-b", core) == aster::Status::kOk);
  assert(generated.CreateModule("test.GeneratedModule", "generated-b", core) ==
         aster::Status::kAlreadyExists);
  assert(generated.modules().size() == 2);
  assert(generated.modules()[0].module.Info().type == "test.GeneratedModule");
  assert(generated.modules()[1].module.Info().type == "test.GeneratedModule");
  assert(generated.CreateModule("test.WrongType", "wrong-type", core) ==
         aster::Status::kTypeMismatch);
  assert(generated.modules().size() == 2);
  aster::Runtime generated_runtime(generated.modules());
  assert(generated_runtime.Initialize() == aster::Status::kOk);
  assert(logger.last_message == "generated initialized");
  assert(logger.last_executor == "lifecycle");
  assert(logger.last_timestamp == 123);
  assert(generated_runtime.Start() == aster::Status::kOk);
  generated_runtime.Shutdown();
  generated.Close();

  // The same exported type can have multiple instances with distinct CoreRefs.
  // Observe the logs emitted by the loaded Modules, not the loader's storage.
  {
    TestLogger first_logger;
    TestLogger second_logger;
    const aster::CoreAdapter first_core(
        {.logger = aster::LoggerRef(first_logger), .instance_name = "first"});
    const aster::CoreAdapter second_core(
        {.logger = aster::LoggerRef(second_logger), .instance_name = "second"});
    const std::array<aster::platform::linux::PackageModule, 2> instances{{
        {"test.GeneratedModule", "first", first_core.ref()},
        {"test.GeneratedModule", "second", second_core.ref()},
    }};
    aster::platform::linux::Supervisor supervisor(core, {});
    assert(supervisor.LoadPackage(ASTER_TEST_CPP_PACKAGE_PATH, instances) == aster::Status::kOk);
    assert(supervisor.Start({}) == aster::Status::kOk);
    assert(first_logger.last_message == "first");
    assert(second_logger.last_message == "second");
    supervisor.Shutdown();
  }

  aster::platform::linux::PluginLoader invalid;
  assert(invalid.Open(ASTER_INVALID_PLUGIN_PATH) == aster::Status::kVersionMismatch);
  assert(!invalid.is_open());
  assert(invalid.Open("/does/not/exist/libaster.so") == aster::Status::kNotFound);
  std::filesystem::remove(trace, error);
}
