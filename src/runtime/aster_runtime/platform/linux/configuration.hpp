#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "aster_module_cpp_interface/logger/logger.hpp"
#include "aster_module_cpp_interface/util/value.hpp"

namespace aster::platform::linux {

using ConfigValue =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string>;
using ConfigValues = std::map<std::string, ConfigValue, std::less<>>;

struct ExecutorConfig {
  std::string name{"main"};
  std::string type{"serial"};
  std::size_t threads{1};
  std::size_t queue_capacity{64};
};

struct PackageConfig {
  std::string name;
  std::string path;
};

struct ModuleConfig {
  std::string name;
  std::string type;
  std::string package;
  bool enabled{true};
  std::string name_space{"/"};
  std::map<std::string, std::string, std::less<>> remap;
  std::string executor{"main"};
  LogLevel log_level{LogLevel::kInfo};
  ConfigValues config;
  ConfigValues parameters;
};

struct RuntimeConfig {
  std::vector<ExecutorConfig> executors;
  std::vector<PackageConfig> packages;
  std::vector<ModuleConfig> modules;
};

// Parser implementation and YAML dependency belong to the Host Runtime only.
// On failure output is unchanged and diagnostic describes the offending field.
Status ParseRuntimeConfig(std::string_view yaml, RuntimeConfig& output,
                          std::string& diagnostic) noexcept;
Status ViewConfigValue(const ConfigValue& value, ValueView& output) noexcept;

}  // namespace aster::platform::linux
