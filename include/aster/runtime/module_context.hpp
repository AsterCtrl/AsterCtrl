#pragma once

#include <cstdint>
#include <string_view>

#include "aster/runtime/executor.hpp"
#include "aster/runtime/hardware_registry.hpp"
#include "aster/runtime/parameter_registry.hpp"
#include "aster/runtime/periodic_scheduler.hpp"
#include "aster/runtime/port_registry.hpp"
#include "aster/runtime/runtime_services.hpp"

namespace aster::runtime {

enum class ModulePhase {
  kConstructed,
  kInitializing,
  kInitialized,
  kStarting,
  kRunning,
  kShuttingDown,
  kStopped,
  kFailed,
};

struct ModuleServices {
  Executor* executor{};
  SteadyClock* clock{};
  LogSink* log{};
  DiagnosticSink* diagnostics{};
  PortResolver* ports{};
  ParameterResolver* parameters{};
  PeriodicTaskBinder* periodic_tasks{};
  HardwareResolver* hardware{};
};

class ModuleContext {
 public:
  constexpr ModuleContext(std::string_view node_name,
                          std::string_view module_name) noexcept
      : node_name_(node_name), module_name_(module_name) {}

  constexpr ModuleContext(std::string_view node_name,
                          std::string_view module_name,
                          Executor& executor) noexcept
      : node_name_(node_name),
        module_name_(module_name),
        services_{.executor = &executor} {}

  constexpr ModuleContext(std::string_view node_name,
                          std::string_view module_name,
                          ModuleServices services) noexcept
      : node_name_(node_name),
        module_name_(module_name),
        services_(services) {}

  constexpr std::string_view node_name() const noexcept { return node_name_; }
  constexpr std::string_view module_name() const noexcept { return module_name_; }
  constexpr ModulePhase phase() const noexcept { return phase_; }
  constexpr Executor* executor() const noexcept { return services_.executor; }
  constexpr SteadyClock* clock() const noexcept { return services_.clock; }
  constexpr LogSink* log() const noexcept { return services_.log; }
  constexpr DiagnosticSink* diagnostics() const noexcept {
    return services_.diagnostics;
  }

  std::uint64_t NowNs() const noexcept {
    return services_.clock == nullptr ? 0 : services_.clock->NowNs();
  }

  Status Log(LogLevel level, std::string_view message,
             const ExecutionContext& caller) const noexcept {
    if (services_.log == nullptr) {
      return Status::kUnavailable;
    }
    return services_.log->Write(
        {node_name_, module_name_, level, message, NowNs()}, caller);
  }

  Status Report(std::string_view name, DiagnosticSeverity severity,
                std::int64_t value,
                const ExecutionContext& caller) const noexcept {
    if (services_.diagnostics == nullptr) {
      return Status::kUnavailable;
    }
    return services_.diagnostics->Report(
        {node_name_, module_name_, name, severity, value, NowNs()}, caller);
  }

  Status BindPeriodicTask(std::string_view name, WorkItem work) const noexcept {
    if (phase_ != ModulePhase::kInitializing) {
      return Status::kInvalidState;
    }
    if (services_.periodic_tasks == nullptr) {
      return Status::kUnavailable;
    }
    return services_.periodic_tasks->BindPeriodicTask(module_name_, name, work);
  }

  template <HardwareDevice Device>
  Status ResolveHardware(std::string_view name, Device*& device) const noexcept {
    device = nullptr;
    if (services_.hardware == nullptr) {
      return Status::kUnavailable;
    }
    void* resolved{};
    const auto status =
        services_.hardware->Resolve(name, Device::TypeName(), resolved);
    if (IsOk(status)) {
      device = static_cast<Device*>(resolved);
    }
    return status;
  }

  template <MessageType Message>
  Status ResolveTopicPublisher(
      std::string_view name,
      TopicPublisher<Message>& publisher) const noexcept {
    publisher = {};
    void* endpoint{};
    const auto status = ResolvePort(
        name, PortKind::kTopicPublisher,
        TypeSupport<Message>::descriptor().schema_hash, endpoint);
    if (IsOk(status)) {
      publisher = TopicPublisher<Message>(
          *static_cast<TopicSource<Message>*>(endpoint));
    }
    return status;
  }

  template <MessageType Message>
  Status ResolveTopicSubscriber(
      std::string_view name,
      TopicSubscriber<Message>& subscriber) const noexcept {
    subscriber = {};
    void* endpoint{};
    const auto status = ResolvePort(
        name, PortKind::kTopicSubscriber,
        TypeSupport<Message>::descriptor().schema_hash, endpoint);
    if (IsOk(status)) {
      if (services_.executor == nullptr) {
        return Status::kUnavailable;
      }
      subscriber = TopicSubscriber<Message>(
          *static_cast<TopicSubscriberEndpoint<Message>*>(endpoint),
          *services_.executor);
    }
    return status;
  }

  template <ServiceType Service>
  Status ResolveServiceClient(
      std::string_view name,
      ServiceClient<Service>& client) const noexcept {
    client = {};
    void* endpoint{};
    const auto status = ResolvePort(
        name, PortKind::kServiceClient,
        ServiceTypeSupport<Service>::descriptor().schema_hash, endpoint);
    if (IsOk(status)) {
      client = ServiceClient<Service>(
          *static_cast<ServiceEndpoint<Service>*>(endpoint));
    }
    return status;
  }

  template <ServiceType Service>
  Status ResolveServiceServer(
      std::string_view name,
      ServiceServer<Service>& server) const noexcept {
    server = {};
    void* endpoint{};
    const auto status = ResolvePort(
        name, PortKind::kServiceServer,
        ServiceTypeSupport<Service>::descriptor().schema_hash, endpoint);
    if (IsOk(status)) {
      server = ServiceServer<Service>(
          *static_cast<ServiceServerEndpoint<Service>*>(endpoint));
    }
    return status;
  }

  template <ActionType Action>
  Status ResolveActionClient(std::string_view name,
                             ActionClient<Action>& client) const noexcept {
    client = {};
    void* endpoint{};
    const auto status = ResolvePort(
        name, PortKind::kActionClient,
        ActionTypeSupport<Action>::descriptor().schema_hash, endpoint);
    if (IsOk(status)) {
      client = ActionClient<Action>(
          *static_cast<ActionClientEndpoint<Action>*>(endpoint));
    }
    return status;
  }

  template <ActionType Action>
  Status ResolveActionServer(std::string_view name,
                             ActionServer<Action>& server) const noexcept {
    server = {};
    void* endpoint{};
    const auto status = ResolvePort(
        name, PortKind::kActionServer,
        ActionTypeSupport<Action>::descriptor().schema_hash, endpoint);
    if (IsOk(status)) {
      server = ActionServer<Action>(
          *static_cast<ActionServerEndpoint<Action>*>(endpoint));
    }
    return status;
  }

  template <RegistryParameterValue Value>
  Status ResolveParameter(std::string_view name,
                          Parameter<Value>*& parameter) const noexcept {
    parameter = nullptr;
    if (services_.parameters == nullptr) {
      return Status::kUnavailable;
    }
    void* resolved{};
    const auto status = services_.parameters->Resolve(
        name, ParameterTypeOf<Value>(), resolved);
    if (IsOk(status)) {
      parameter = static_cast<Parameter<Value>*>(resolved);
    }
    return status;
  }

 private:
  friend class Runtime;

  constexpr void SetPhase(ModulePhase phase) noexcept { phase_ = phase; }

  Status ResolvePort(std::string_view name, PortKind kind,
                     SchemaHash schema_hash,
                     void*& endpoint) const noexcept {
    if (services_.ports == nullptr) {
      endpoint = nullptr;
      return Status::kUnavailable;
    }
    return services_.ports->Resolve(name, kind, schema_hash, endpoint);
  }

  std::string_view node_name_;
  std::string_view module_name_;
  ModuleServices services_{};
  ModulePhase phase_{ModulePhase::kConstructed};
};

}  // namespace aster::runtime
