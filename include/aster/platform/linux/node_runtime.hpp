#pragma once

#include <cstddef>
#include <string_view>

#include "aster/channel.hpp"
#include "aster/platform/linux/runtime_services.hpp"
#include "aster/platform/linux/supervisor.hpp"
#include "aster/rpc.hpp"
#include "aster/rpc_router.hpp"
#include "aster/static_hardware.hpp"
#include "aster/status.hpp"
#include "aster/transport/peer_registry.hpp"

namespace aster::platform::linux {

template <typename Composition, std::size_t MaxTopics, std::size_t MaxSubscribersPerTopic,
          std::size_t MaximumMessageSize, std::size_t MaxRpcServices, std::size_t MaxPendingRpc,
          std::size_t MaxHardwareCapabilities, std::size_t ExecutorQueueDepth>
class StaticNodeRuntime final {
 public:
  static_assert(MaxTopics > 0);
  static_assert(MaxSubscribersPerTopic > 0);
  static_assert(MaximumMessageSize > 0);
  static_assert(MaxRpcServices > 0);
  static_assert(MaxPendingRpc > 0);
  static_assert(MaxHardwareCapabilities > 0);
  static_assert(ExecutorQueueDepth > 0);

  StaticNodeRuntime(transport::DeploymentId deployment_id, std::string_view executor_name) noexcept
      : executor_(executor_name, clock_),
        local_rpc_(ExecutorRef(executor_)),
        rpc_(local_rpc_),
        core_(CoreHandles{
            .configurator = {},
            .logger = LoggerRef(logger_),
            .executor = ExecutorRef(executor_),
            .channel = ChannelRef(channel_),
            .rpc = RpcRef(rpc_),
            .parameter = {},
            .clock = ClockRef(clock_),
            .allocator = AllocatorRef(allocator_),
            .hardware = HardwareManagerRef(hardware_),
        }),
        composition_(core_),
        deployment_id_(deployment_id),
        supervisor_(core_, deployment_id_, Lifecycle(executor_)) {
    Record(supervisor_.AddRegistry(channel_));
    Record(supervisor_.AddRegistry(rpc_));
    Record(supervisor_.AddRegistry(hardware_));
  }

  ~StaticNodeRuntime() { Shutdown(); }

  StaticNodeRuntime(const StaticNodeRuntime&) = delete;
  StaticNodeRuntime& operator=(const StaticNodeRuntime&) = delete;
  StaticNodeRuntime(StaticNodeRuntime&&) = delete;
  StaticNodeRuntime& operator=(StaticNodeRuntime&&) = delete;

  Status AddInfrastructureModule(Module& module, std::string_view instance_name) noexcept {
    if (finalized_ || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    const auto status = supervisor_.AddModule({&module, core_, instance_name});
    Record(status);
    return status;
  }

  Status AddRegistry(Registry& registry) noexcept {
    if (finalized_ || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    const auto status = supervisor_.AddRegistry(registry);
    Record(status);
    return status;
  }

  Status RegisterRemoteRpc(const ServiceDescriptor& descriptor, RpcBackend& backend) noexcept {
    if (finalized_ || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    const auto status = rpc_.AddRemoteClient(descriptor, backend);
    Record(status);
    return status;
  }

  Status RegisterHardware(std::string_view name, std::string_view type, void* device) noexcept {
    if (finalized_ || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    const auto status = hardware_.Register(name, type, device);
    Record(status);
    return status;
  }

  Status Start() noexcept {
    if (finalized_ || !IsOk(setup_status_)) {
      return finalized_ ? Status::kInvalidState : setup_status_;
    }
    finalized_ = true;
    for (const auto& slot : composition_.Modules()) {
      Record(supervisor_.AddModule(slot));
    }
    for (const auto& slot : composition_.Registries()) {
      if (slot.registry == nullptr) {
        Record(Status::kInvalidArgument);
      } else {
        Record(supervisor_.AddRegistry(*slot.registry));
      }
    }
    return IsOk(setup_status_) ? supervisor_.Start(deployment_id_) : setup_status_;
  }

  void Shutdown() noexcept { supervisor_.Shutdown(); }

  [[nodiscard]] SupervisorState state() const noexcept { return supervisor_.state(); }
  [[nodiscard]] Status setup_status() const noexcept { return setup_status_; }
  [[nodiscard]] CoreRef core() const noexcept { return core_; }
  [[nodiscard]] Composition& composition() noexcept { return composition_; }

 private:
  template <std::size_t Capacity>
  static ExecutorLifecycle Lifecycle(ThreadExecutor<Capacity>& executor) noexcept {
    return {
        [](void* state) noexcept {
          return static_cast<ThreadExecutor<Capacity>*>(state)->Prepare();
        },
        [](void* state) noexcept {
          return static_cast<ThreadExecutor<Capacity>*>(state)->Activate();
        },
        [](void* state) noexcept { static_cast<ThreadExecutor<Capacity>*>(state)->Shutdown(); },
        &executor,
    };
  }

  void Record(Status status) noexcept {
    if (IsOk(setup_status_) && !IsOk(status)) {
      setup_status_ = status;
    }
  }

  SteadyClock clock_;
  StderrLogger logger_;
  SystemAllocator allocator_;
  ThreadExecutor<ExecutorQueueDepth> executor_;
  LocalChannel<MaxTopics, MaxSubscribersPerTopic, MaximumMessageSize> channel_;
  LocalRpc<MaxRpcServices, MaximumMessageSize, MaximumMessageSize, MaxPendingRpc> local_rpc_;
  RpcRouter<MaxRpcServices> rpc_;
  StaticHardwareManager<MaxHardwareCapabilities> hardware_;
  CoreRef core_;
  Composition composition_;
  transport::DeploymentId deployment_id_;
  Supervisor supervisor_;
  Status setup_status_{Status::kOk};
  bool finalized_{};
};

}  // namespace aster::platform::linux
