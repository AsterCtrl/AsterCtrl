#pragma once

#include <memory>

#include "aster_runtime/core/rpc.hpp"
#include "aster_runtime/platform/linux/configuration.hpp"
#include "aster_runtime/platform/linux/runtime_services.hpp"

namespace aster::platform::linux {

class RpcManager final : public Registry {
 public:
  explicit RpcManager(ClockRef clock);
  ~RpcManager() override;
  RpcManager(const RpcManager&) = delete;
  RpcManager& operator=(const RpcManager&) = delete;

  std::unique_ptr<RpcBackend> CreateEndpoint(const ModuleConfig& config,
                                             ThreadExecutor<0>& executor);
  Status Seal() noexcept override;
  [[nodiscard]] bool sealed() const noexcept override;
  void Close() noexcept;
  // Executors must be quiescent. Outstanding completions receive Cancelled on
  // the shutdown thread while their Module instances are still alive.
  void CancelPending() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace aster::platform::linux
