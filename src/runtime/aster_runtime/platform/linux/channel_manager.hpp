#pragma once

#include <memory>

#include "aster_module_cpp_interface/executor.hpp"
#include "aster_runtime/core/channel.hpp"
#include "aster_runtime/platform/linux/configuration.hpp"

namespace aster::platform::linux {

// Owns the process-wide Topic registry. Each endpoint applies its instance's
// namespace/remap and dispatches subscriptions on that instance's executor.
class ChannelManager final : public Registry {
 public:
  ChannelManager();
  ~ChannelManager() override;
  ChannelManager(const ChannelManager&) = delete;
  ChannelManager& operator=(const ChannelManager&) = delete;

  std::unique_ptr<ChannelBackend> CreateEndpoint(const ModuleConfig& config, ExecutorRef executor);
  Status Seal() noexcept override;
  [[nodiscard]] bool sealed() const noexcept override;
  void Close() noexcept;
  // Call only after all executors are quiescent and before unloading Packages.
  void ClearPending() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace aster::platform::linux
