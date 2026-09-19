#include "aster_runtime/core_adapter.hpp"
#include "aster_runtime/platform/linux/channel_manager.hpp"
#include "aster_runtime/platform/linux/rpc_manager.hpp"
#include "aster_runtime/platform/linux/runtime.hpp"
#include "aster_runtime/platform/linux/runtime_services.hpp"
#include "instance_configurator.hpp"

namespace aster::platform::linux {

struct NodeRuntime::Impl {
  struct Instance {
    Instance(const ModuleConfig& config, ThreadExecutor<0>& executor, ChannelManager& channels,
             RpcManager& rpcs, SteadyClock& clock, SystemAllocator& allocator)
        : configurator(config.config),
          parameters(config.parameters),
          logger(config.name, config.log_level),
          channel(channels.CreateEndpoint(config, ExecutorRef(executor))),
          rpc(rpcs.CreateEndpoint(config, executor)),
          core({.configurator = ConfiguratorRef(configurator),
                .logger = LoggerRef(logger),
                .executor = ExecutorRef(executor),
                .channel = ChannelRef(*channel),
                .rpc = RpcRef(*rpc),
                .parameter = ParameterRef(parameters),
                .clock = ClockRef(clock),
                .allocator = AllocatorRef(allocator),
                .hardware = {},
                .instance_name = config.name}) {}
    InstanceConfigurator configurator;
    InstanceParameters parameters;
    StderrLogger logger;
    std::unique_ptr<ChannelBackend> channel;
    std::unique_ptr<RpcBackend> rpc;
    CoreAdapter core;
  };

  static Status Prepare(void* state) noexcept {
    auto& self = *static_cast<Impl*>(state);
    for (const auto& executor : self.executors) {
      const auto status = executor->Prepare();
      if (!IsOk(status)) {
        Quiesce(state);
        return status;
      }
    }
    return Status::kOk;
  }
  static Status Activate(void* state) noexcept {
    auto& self = *static_cast<Impl*>(state);
    for (const auto& executor : self.executors) {
      const auto status = executor->Activate();
      if (!IsOk(status)) {
        Quiesce(state);
        return status;
      }
    }
    return Status::kOk;
  }
  static void Quiesce(void* state) noexcept {
    auto& self = *static_cast<Impl*>(state);
    self.channel.Close();
    self.rpc.Close();
    for (auto it = self.executors.rbegin(); it != self.executors.rend(); ++it) (*it)->Shutdown();
    self.channel.ClearPending();
    self.rpc.CancelPending();
  }

  RuntimeConfig config;
  std::string diagnostic;
  SteadyClock clock;
  SystemAllocator allocator;
  ChannelManager channel;
  RpcManager rpc{ClockRef(clock)};
  std::map<std::string, ModuleBase*, std::less<>> static_modules;
  std::vector<std::unique_ptr<ThreadExecutor<0>>> executors;
  std::vector<std::unique_ptr<Instance>> instances;
  std::vector<std::unique_ptr<PluginLoader>> packages;
  std::unique_ptr<Supervisor> supervisor;
  SupervisorState state{SupervisorState::kComposing};
};

NodeRuntime::NodeRuntime() : impl_(std::make_unique<Impl>()) {}
NodeRuntime::~NodeRuntime() { Shutdown(); }
Status NodeRuntime::RegisterModule(std::string_view name, ModuleBase& module) noexcept {
  if (impl_->state != SupervisorState::kComposing) return Status::kInvalidState;
  if (name.empty()) return Status::kInvalidArgument;
  try {
    return impl_->static_modules.emplace(name, &module).second ? Status::kOk
                                                               : Status::kAlreadyExists;
  } catch (const std::bad_alloc&) {
    return Status::kCapacityExceeded;
  } catch (...) {
    return Status::kInternal;
  }
}

Status NodeRuntime::Initialize(std::string_view yaml, const std::filesystem::path& base) noexcept {
  auto& self = *impl_;
  if (self.state != SupervisorState::kComposing) return Status::kInvalidState;
  auto status = ParseRuntimeConfig(yaml, self.config, self.diagnostic);
  if (!IsOk(status)) return status;
  try {
    self.supervisor = std::make_unique<Supervisor>(
        CoreRef{}, transport::DeploymentId{},
        ExecutorLifecycle{Impl::Prepare, Impl::Activate, Impl::Quiesce, &self});
    status = self.supervisor->AddRegistry(self.channel);
    if (IsOk(status)) status = self.supervisor->AddRegistry(self.rpc);
    std::map<std::string, ThreadExecutor<0>*, std::less<>> executors;
    for (const auto& config : self.config.executors) {
      auto executor = std::make_unique<ThreadExecutor<0>>(config.name, self.clock,
                                                          config.queue_capacity, config.threads);
      executors.emplace(config.name, executor.get());
      self.executors.push_back(std::move(executor));
    }
    std::map<std::string, PluginLoader*, std::less<>> packages;
    for (const auto& config : self.config.modules) {
      if (!IsOk(status)) break;
      if (!config.enabled) continue;
      self.diagnostic = "initializing instance: " + config.name;
      auto instance =
          std::make_unique<Impl::Instance>(config, *executors.at(config.executor), self.channel,
                                           self.rpc, self.clock, self.allocator);
      const auto core = instance->core.ref();
      self.instances.push_back(std::move(instance));
      if (config.package.empty()) {
        const auto module = self.static_modules.find(config.name);
        if (module == self.static_modules.end()) {
          status = Status::kNotFound;
          break;
        }
        if (module->second->Info().type != config.type) {
          status = Status::kTypeMismatch;
          break;
        }
        status = self.supervisor->AddModule({module->second, core, config.name});
      } else {
        auto found = packages.find(config.package);
        if (found == packages.end()) {
          const auto package =
              std::find_if(self.config.packages.begin(), self.config.packages.end(),
                           [&](const auto& value) { return value.name == config.package; });
          auto loader = std::make_unique<PluginLoader>();
          const auto path = base / package->path;
          status = loader->Open(path.string());
          if (!IsOk(status)) break;
          found = packages.emplace(config.package, loader.get()).first;
          self.packages.push_back(std::move(loader));
        }
        status = found->second->CreateModule(config.type, config.name, core);
        if (IsOk(status)) status = self.supervisor->AddModule(found->second->modules().back());
      }
    }
    if (IsOk(status)) status = self.supervisor->Initialize();
    if (IsOk(status)) {
      self.diagnostic.clear();
      self.state = SupervisorState::kInitialized;
      return status;
    }
    if (const auto* failure = self.supervisor->failure())
      self.diagnostic = "initialization failed: " + std::string(failure->name);
  } catch (const std::bad_alloc&) {
    status = Status::kCapacityExceeded;
  } catch (const std::exception& error) {
    try {
      self.diagnostic = error.what();
    } catch (...) {
      self.diagnostic.clear();
    }
    status = Status::kInternal;
  } catch (...) {
    status = Status::kInternal;
  }
  Shutdown();
  self.state = SupervisorState::kFailed;
  return status;
}

Status NodeRuntime::Start() noexcept {
  if (impl_->state != SupervisorState::kInitialized) return Status::kInvalidState;
  const auto status = impl_->supervisor->Start({});
  impl_->state = IsOk(status) ? SupervisorState::kRunning : SupervisorState::kFailed;
  if (!IsOk(status)) {
    try {
      const auto* failure = impl_->supervisor->failure();
      impl_->diagnostic = failure == nullptr ? "executor activation failed"
                                             : "start failed: " + std::string(failure->name);
    } catch (...) {
      impl_->diagnostic.clear();
    }
    Shutdown();
    impl_->state = SupervisorState::kFailed;
  }
  return status;
}
void NodeRuntime::Shutdown() noexcept {
  if (impl_->supervisor) impl_->supervisor->Shutdown();
  Impl::Quiesce(impl_.get());
  impl_->supervisor.reset();
  while (!impl_->packages.empty()) impl_->packages.pop_back();
  impl_->state = SupervisorState::kStopped;
}
SupervisorState NodeRuntime::state() const noexcept { return impl_->state; }
std::string_view NodeRuntime::diagnostic() const noexcept { return impl_->diagnostic; }
Status NodeRuntime::VisitGraph(GraphVisitor visitor, void* state) const noexcept {
  return impl_->supervisor ? impl_->supervisor->VisitGraph(visitor, state) : Status::kInvalidState;
}

}  // namespace aster::platform::linux
