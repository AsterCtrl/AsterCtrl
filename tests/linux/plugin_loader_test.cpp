#include "aster/platform/linux/plugin_loader.hpp"

#include <unistd.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "aster/runtime.hpp"

#ifndef ASTER_TEST_PLUGIN_PATH
#error ASTER_TEST_PLUGIN_PATH must be defined
#endif

#ifndef ASTER_INVALID_PLUGIN_PATH
#error ASTER_INVALID_PLUGIN_PATH must be defined
#endif

namespace {

class TestConfigurator final : public aster::Configurator {
 public:
  aster::Status Get(std::string_view key, std::span<std::byte> output,
                    std::size_t& written) const noexcept override {
    written = 0;
    if (key != "answer") {
      return aster::Status::kNotFound;
    }
    constexpr std::uint32_t answer = 42;
    if (output.size() < sizeof(answer)) {
      return aster::Status::kCapacityExceeded;
    }
    std::memcpy(output.data(), &answer, sizeof(answer));
    written = sizeof(answer);
    return aster::Status::kOk;
  }
};

class TestLogger final : public aster::Logger {
 public:
  aster::Status Write(aster::LogLevel level, std::string_view message,
                      const aster::ExecutionContext&) noexcept override {
    last_level = level;
    last_message.assign(message);
    return aster::Status::kOk;
  }

  aster::LogLevel last_level{aster::LogLevel::kTrace};
  std::string last_message;
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
  aster::Status Get(std::string_view, std::string_view, std::span<std::byte>,
                    std::size_t& written) const noexcept override {
    written = 0;
    return aster::Status::kNotFound;
  }
  aster::Status Set(std::string_view, std::string_view, std::span<const std::byte>,
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
  aster::platform::linux::PluginLoader loader;
  assert(loader.Open(ASTER_TEST_PLUGIN_PATH, core) == aster::Status::kOk);
  assert(loader.is_open());
  assert(loader.name() == "test-plugin");
  assert(loader.version() == "1.0.0");
  assert(loader.modules().size() == 1);
  assert(loader.modules()[0].module->Info().name == "loaded");

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
  assert(events == "ITSBP");

  aster::platform::linux::PluginLoader invalid;
  assert(invalid.Open(ASTER_INVALID_PLUGIN_PATH, {}) == aster::Status::kVersionMismatch);
  assert(!invalid.is_open());
  assert(invalid.Open("/does/not/exist/libaster.so", {}) == aster::Status::kNotFound);
  std::filesystem::remove(trace, error);
}
