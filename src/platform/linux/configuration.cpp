#include "aster_runtime/platform/linux/configuration.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <stdexcept>

#include "aster_runtime/name_resolver.hpp"

namespace aster::platform::linux {
namespace {

void CheckMap(const YAML::Node& node, std::initializer_list<std::string_view> fields) {
  if (!node.IsMap()) throw std::invalid_argument("expected a mapping");
  std::set<std::string> seen;
  for (const auto& entry : node) {
    const auto key = entry.first.as<std::string>();
    if (!seen.insert(key).second) throw std::invalid_argument("duplicate key: " + key);
    if (std::find(fields.begin(), fields.end(), key) == fields.end())
      throw std::invalid_argument("unknown field: " + key);
  }
}

std::string RequiredString(const YAML::Node& node, const char* field) {
  if (!node[field] || !node[field].IsScalar() || node[field].Scalar().empty())
    throw std::invalid_argument(std::string("required nonempty field: ") + field);
  return node[field].Scalar();
}

template <typename T>
T Optional(const YAML::Node& node, const char* field, T fallback) {
  return node[field] ? node[field].as<T>() : std::move(fallback);
}

LogLevel ParseLevel(const YAML::Node& node, LogLevel fallback) {
  if (!node) return fallback;
  const auto text = node.as<std::string>();
  constexpr std::string_view levels[]{"trace", "debug", "info", "warning", "error", "critical"};
  for (std::size_t i = 0; i < std::size(levels); ++i)
    if (levels[i] == text) return static_cast<LogLevel>(i);
  throw std::invalid_argument("unsupported log level: " + text);
}

ConfigValue ParseValue(const YAML::Node& node) {
  if (node.IsNull()) return std::monostate{};
  if (!node.IsScalar())
    throw std::invalid_argument("config values must be scalars or nested mappings");
  const auto& text = node.Scalar();
  const auto& tag = node.Tag();
  if (tag == "!" || tag == "tag:yaml.org,2002:str") return text;
  if (tag != "?" && !tag.empty()) throw std::invalid_argument("unsupported scalar tag: " + tag);
  if (text == "true") return true;
  if (text == "false") return false;
  std::int64_t integer{};
  const auto signed_result = std::from_chars(text.data(), text.data() + text.size(), integer);
  if (signed_result.ec == std::errc{} && signed_result.ptr == text.data() + text.size())
    return integer;
  std::uint64_t unsigned_integer{};
  const auto unsigned_result =
      std::from_chars(text.data(), text.data() + text.size(), unsigned_integer);
  if (unsigned_result.ec == std::errc{} && unsigned_result.ptr == text.data() + text.size())
    return unsigned_integer;
  if (signed_result.ec == std::errc::result_out_of_range ||
      unsigned_result.ec == std::errc::result_out_of_range)
    throw std::invalid_argument("integer outside 64-bit range: " + text);
  if (text.find_first_of(".eE") != std::string::npos) {
    try {
      const auto real = node.as<double>();
      if (!std::isfinite(real)) throw std::invalid_argument("non-finite config value: " + text);
      return real;
    } catch (const YAML::BadConversion&) {
      // An ordinary unquoted string may contain dots or the letter e.
      return text;
    }
  }
  return text;
}

void ParseValues(const YAML::Node& node, ConfigValues& values, const std::string& prefix = {},
                 std::size_t depth = 0) {
  if (!node) return;
  if (!node.IsMap() || depth > 32)
    throw std::invalid_argument("config must be a finite nested mapping");
  std::set<std::string> seen;
  for (const auto& entry : node) {
    const auto key = entry.first.as<std::string>();
    if (key.empty() || key.find('.') != std::string::npos || !seen.insert(key).second)
      throw std::invalid_argument("invalid or duplicate config key: " + key);
    auto path = prefix;
    if (!path.empty()) path += '.';
    path += key;
    if (entry.second.IsMap())
      ParseValues(entry.second, values, path, depth + 1);
    else
      values.emplace(path, ParseValue(entry.second));
  }
}

void CheckSequence(const YAML::Node& node, const char* field) {
  if (node && !node.IsSequence())
    throw std::invalid_argument(std::string(field) + " must be a list");
}

}  // namespace

Status ViewConfigValue(const ConfigValue& value, ValueView& output) noexcept {
  try {
    output = std::visit(
        [](const auto& item) noexcept -> ValueView {
          using T = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<T, std::monostate>)
            return {};
          else if constexpr (std::is_same_v<T, std::string>)
            return ValueView(std::string_view(item));
          else
            return ValueView(item);
        },
        value);
    return Status::kOk;
  } catch (...) {
    return Status::kInternal;
  }
}

Status ParseRuntimeConfig(std::string_view yaml, RuntimeConfig& output,
                          std::string& diagnostic) noexcept {
  try {
    const auto root = YAML::Load(std::string(yaml));
    if (!root.IsMap() || !root["api_version"] ||
        root["api_version"].as<std::string>() != "aster.dev/v1alpha3") {
      diagnostic =
          "expected api_version: aster.dev/v1alpha3; migrate legacy Application/Deployment config "
          "to runtime.yaml";
      return Status::kVersionMismatch;
    }
    CheckMap(root, {"api_version", "aster"});
    const auto aster = root["aster"];
    CheckMap(aster, {"packages", "modules", "executors", "logging", "channel", "rpc"});
    RuntimeConfig config;
    LogLevel default_level = LogLevel::kInfo;
    if (const auto logging = aster["logging"]) {
      CheckMap(logging, {"level"});
      default_level = ParseLevel(logging["level"], default_level);
    }
    for (const auto* kind : {"channel", "rpc"}) {
      if (const auto communication = aster[kind]) {
        CheckMap(communication, {"backends"});
        const auto backends = communication["backends"];
        if (!backends || !backends.IsSequence() || backends.size() != 1 ||
            backends[0].as<std::string>() != "local")
          throw std::invalid_argument(std::string(kind) +
                                      ": this runtime currently supports backends: [local]");
      }
    }
    std::set<std::string> executors;
    CheckSequence(aster["executors"], "executors");
    for (const auto& item : aster["executors"]) {
      CheckMap(item, {"name", "type", "threads", "queue_capacity"});
      ExecutorConfig executor;
      executor.name = RequiredString(item, "name");
      executor.type = Optional<std::string>(item, "type", "serial");
      executor.threads = Optional<std::size_t>(item, "threads", 1);
      executor.queue_capacity = Optional<std::size_t>(item, "queue_capacity", 64);
      if ((executor.type != "serial" && executor.type != "thread_pool") ||
          (executor.type == "serial" && executor.threads != 1) || executor.threads == 0 ||
          executor.queue_capacity == 0)
        throw std::invalid_argument("unsupported executor policy: " + executor.name);
      if (!executors.insert(executor.name).second)
        throw std::invalid_argument("duplicate executor: " + executor.name);
      config.executors.push_back(std::move(executor));
    }
    if (config.executors.empty()) {
      config.executors.emplace_back();
      executors.insert("main");
    }
    std::set<std::string> packages;
    CheckSequence(aster["packages"], "packages");
    for (const auto& item : aster["packages"]) {
      CheckMap(item, {"name", "path"});
      PackageConfig package{RequiredString(item, "name"), RequiredString(item, "path")};
      if (!packages.insert(package.name).second)
        throw std::invalid_argument("duplicate package: " + package.name);
      config.packages.push_back(std::move(package));
    }
    std::set<std::string> instances;
    CheckSequence(aster["modules"], "modules");
    for (const auto& item : aster["modules"]) {
      CheckMap(item, {"name", "type", "package", "enabled", "namespace", "remap", "executor",
                      "log_level", "config", "parameters"});
      ModuleConfig module;
      module.name = RequiredString(item, "name");
      module.type = RequiredString(item, "type");
      module.package = Optional<std::string>(item, "package", "");
      module.enabled = Optional<bool>(item, "enabled", true);
      module.name_space = Optional<std::string>(item, "namespace", "/");
      module.executor = Optional<std::string>(item, "executor", config.executors.front().name);
      module.log_level = ParseLevel(item["log_level"], default_level);
      if (!instances.insert(module.name).second)
        throw std::invalid_argument("duplicate module instance: " + module.name);
      if (!executors.contains(module.executor))
        throw std::invalid_argument("unknown executor: " + module.executor);
      if (!module.package.empty() && !packages.contains(module.package))
        throw std::invalid_argument("unknown package: " + module.package);
      if (const auto remap = item["remap"]) {
        if (!remap.IsMap()) throw std::invalid_argument("remap must be a mapping");
        for (const auto& rule : remap)
          if (!module.remap.emplace(rule.first.as<std::string>(), rule.second.as<std::string>())
                   .second)
            throw std::invalid_argument("duplicate remap in: " + module.name);
      }
      std::vector<NameRemap> remaps;
      remaps.reserve(module.remap.size());
      for (const auto& [from, to] : module.remap) remaps.push_back({from, to});
      if (!IsOk(NameResolver(module.name_space, remaps).Validate()))
        throw std::invalid_argument("invalid namespace/remap for: " + module.name);
      ParseValues(item["config"], module.config);
      ParseValues(item["parameters"], module.parameters);
      config.modules.push_back(std::move(module));
    }
    output = std::move(config);
    diagnostic.clear();
    return Status::kOk;
  } catch (const std::bad_alloc&) {
    diagnostic.clear();
    return Status::kCapacityExceeded;
  } catch (const std::exception& error) {
    try {
      diagnostic = error.what();
    } catch (...) {
      diagnostic.clear();
    }
    return Status::kInvalidArgument;
  } catch (...) {
    diagnostic.clear();
    return Status::kInternal;
  }
}

}  // namespace aster::platform::linux
