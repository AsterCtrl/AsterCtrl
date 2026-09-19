#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include "aster_runtime/platform/linux/configuration.hpp"
#include "aster_runtime/platform/linux/runtime.hpp"
#include "aster_runtime/platform/linux/shutdown_signal.hpp"

int main(int argc, char** argv) {
  using namespace aster::platform::linux;
  try {
    std::filesystem::path config_path;
    bool check{};
    bool validate_config{};
    std::uint32_t duration_ms{};
    for (int i = 1; i < argc; ++i) {
      const std::string_view argument(argv[i]);
      if (argument == "--help") {
        std::cout << "aster_runtime --config runtime.yaml "
                     "[--check | --validate-config] [--duration-ms N]\n";
        return 0;
      }
      if (argument == "--check") {
        check = true;
        continue;
      }
      if (argument == "--validate-config") {
        validate_config = true;
        continue;
      }
      if (argument == "--config" && i + 1 < argc) {
        config_path = argv[++i];
        continue;
      }
      if (argument == "--duration-ms" && i + 1 < argc) {
        const std::string_view number(argv[++i]);
        const auto parsed =
            std::from_chars(number.data(), number.data() + number.size(), duration_ms);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
            duration_ms == 0)
          throw std::invalid_argument("--duration-ms requires a positive 32-bit integer");
        continue;
      }
      throw std::invalid_argument("unknown or incomplete option: " + std::string(argument));
    }
    if (config_path.empty()) throw std::invalid_argument("--config runtime.yaml is required");
    if ((check && validate_config) || ((check || validate_config) && duration_ms != 0))
      throw std::invalid_argument("validation modes cannot be combined with starting options");
    config_path = std::filesystem::absolute(config_path);
    std::ifstream input(config_path);
    if (!input) throw std::invalid_argument("cannot read config: " + config_path.string());
    const std::string yaml((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("failed to read config: " + config_path.string());
    if (validate_config) {
      RuntimeConfig config;
      std::string diagnostic;
      const auto status = ParseRuntimeConfig(yaml, config, diagnostic);
      if (!aster::IsOk(status)) {
        std::cerr << "configuration failed (0x" << std::hex << static_cast<std::uint32_t>(status)
                  << "): " << diagnostic << '\n';
        return 1;
      }
      std::cout << "configuration validated (Module registrations not checked)\n";
      return 0;
    }
    ShutdownSignal signal;
    auto status = signal.Install();
    if (!aster::IsOk(status)) throw std::runtime_error("cannot install shutdown signal handlers");
    NodeRuntime runtime;
    status = runtime.Initialize(yaml, config_path.parent_path());
    if (aster::IsOk(status) && !check) status = runtime.Start();
    if (!aster::IsOk(status)) {
      std::cerr << "Runtime failed (0x" << std::hex << static_cast<std::uint32_t>(status)
                << "): " << runtime.diagnostic() << '\n';
      return 1;
    }
    if (check) {
      std::cout << "configuration and Module registrations validated\n";
    } else {
      const auto started = std::chrono::steady_clock::now();
      while (!signal.requested() &&
             (duration_ms == 0 ||
              std::chrono::steady_clock::now() - started < std::chrono::milliseconds(duration_ms)))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    runtime.Shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
