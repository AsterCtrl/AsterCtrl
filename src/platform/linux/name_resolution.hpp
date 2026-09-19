#pragma once

#include "aster_runtime/name_resolver.hpp"
#include "aster_runtime/platform/linux/configuration.hpp"

namespace aster::platform::linux {

inline Status ResolveInstanceName(const ModuleConfig& config, std::string_view requested,
                                  std::string& output) {
  std::vector<NameRemap> remaps;
  auto capacity = config.name_space.size() + requested.size() + 1;
  for (const auto& [from, to] : config.remap) {
    remaps.push_back({from, to});
    capacity = std::max(capacity, to.size());
  }
  output.resize(capacity);
  std::string_view resolved;
  const auto status = NameResolver(config.name_space, remaps).Resolve(requested, output, resolved);
  if (IsOk(status)) output.resize(resolved.size());
  return status;
}

}  // namespace aster::platform::linux
