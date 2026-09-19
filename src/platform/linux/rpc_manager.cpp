#include "aster_runtime/platform/linux/rpc_manager.hpp"

#include <atomic>
#include <cstdio>
#include <list>

#include "name_resolution.hpp"

namespace aster::platform::linux {

struct RpcManager::Impl {
  using Key = std::pair<std::string, std::string>;
  struct Service {
    std::string instance_name, method_name, request_name, response_name;
    ServiceDescriptor descriptor;
    RawRpcHandler handler{};
    void* handler_state{};
    ThreadExecutor<0>* executor{};
    std::size_t clients{};
  };
  struct Call {
    Impl* owner{};
    Service* service{};
    ThreadExecutor<0>* caller{};
    RawRpcCompletion completion{};
    void* completion_state{};
    std::vector<std::byte> request, response;
    std::size_t response_size{};
    aster_rpc_call_info_t info{};
    std::uint64_t ticket{};
    Status status{Status::kOk};
    bool server_done{}, completion_done{};
    std::list<Call>::iterator position;
  };

  class Endpoint final : public RpcBackend {
   public:
    Endpoint(Impl& owner, const ModuleConfig& config, ThreadExecutor<0>& executor)
        : owner_(owner), config_(config), executor_(executor) {}
    Status RegisterClient(const ServiceDescriptor& descriptor) noexcept override {
      if (descriptor.instance_name.empty()) return Status::kInvalidArgument;
      return Register(descriptor, nullptr, nullptr);
    }
    Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                          void* state) noexcept override {
      return handler == nullptr ? Status::kInvalidArgument : Register(descriptor, handler, state);
    }
    Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                     std::uint64_t deadline, RawRpcCompletion completion, void* state,
                     const ExecutionContext& caller) noexcept override {
      try {
        const std::lock_guard lock(owner_.mutex);
        if (!owner_.sealed || !owner_.accepting) return Status::kInvalidState;
        if (completion == nullptr) return Status::kInvalidArgument;
        const auto binding =
            clients_.find({std::string(descriptor.instance_name), std::string(descriptor.name)});
        if (binding == clients_.end()) return Status::kNotFound;
        auto* service = binding->second;
        auto resolved = descriptor;
        resolved.instance_name = service->descriptor.instance_name;
        if (!SameService(resolved, service->descriptor)) return Status::kTypeMismatch;
        if (request.size() > descriptor.request_type.max_serialized_size)
          return Status::kCapacityExceeded;
        if (deadline != 0 && caller.timestamp_ns() >= deadline) return Status::kTimeout;
        if (service->handler == nullptr) return Status::kUnavailable;
        Call call;
        call.owner = &owner_;
        call.service = service;
        call.caller = &executor_;
        call.completion = completion;
        call.completion_state = state;
        call.request.assign(request.begin(), request.end());
        call.response.resize(service->descriptor.response_type.max_serialized_size);
        owner_.next_id = owner_.next_id == UINT32_MAX ? 1 : owner_.next_id + 1;
        call.info = {owner_.next_id, deadline};
        auto pending = owner_.pending.emplace(owner_.pending.end(), std::move(call));
        pending->position = pending;
        auto status = executor_.Reserve(WorkItem::Bind<Complete>(&*pending), pending->ticket);
        if (!IsOk(status)) {
          owner_.pending.erase(pending);
          return status;
        }
        status = service->executor->TryPost(WorkItem::Bind<Invoke>(&*pending), caller);
        if (!IsOk(status)) {
          executor_.CancelReserved(pending->ticket);
          owner_.pending.erase(pending);
          return status;
        }
        // The manager lock keeps the server from completing before admission
        // is finished. With no deadline, the reserved completion stays dormant.
        if (deadline != 0) executor_.ScheduleReserved(pending->ticket, deadline);
        return Status::kOk;
      } catch (const std::bad_alloc&) {
        return Status::kCapacityExceeded;
      } catch (...) {
        return Status::kInternal;
      }
    }
    Status Seal() noexcept override { return Status::kInvalidState; }
    bool sealed() const noexcept override { return owner_.sealed; }

   private:
    Status Register(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                    void* state) noexcept {
      try {
        const std::lock_guard lock(owner_.mutex);
        if (owner_.sealed) return Status::kInvalidState;
        if (descriptor.name.empty() || descriptor.request_type.name.empty() ||
            descriptor.response_type.name.empty() ||
            descriptor.request_type.max_serialized_size == 0 ||
            descriptor.response_type.max_serialized_size == 0)
          return Status::kInvalidArgument;
        std::string instance;
        const auto status =
            ResolveInstanceName(config_,
                                descriptor.instance_name.empty() ? std::string_view(config_.name)
                                                                 : descriptor.instance_name,
                                instance);
        if (!IsOk(status)) return status;
        const Key key{instance, descriptor.name};
        auto found = owner_.services.find(key);
        if (found == owner_.services.end()) {
          auto service = std::make_unique<Service>();
          service->instance_name = instance;
          service->method_name = descriptor.name;
          service->request_name = descriptor.request_type.name;
          service->response_name = descriptor.response_type.name;
          service->descriptor = descriptor;
          service->descriptor.instance_name = service->instance_name;
          service->descriptor.name = service->method_name;
          service->descriptor.request_type.name = service->request_name;
          service->descriptor.response_type.name = service->response_name;
          found = owner_.services.emplace(key, std::move(service)).first;
        }
        auto& service = *found->second;
        auto resolved = descriptor;
        resolved.instance_name = instance;
        if (!SameService(resolved, service.descriptor)) return Status::kTypeMismatch;
        if (handler != nullptr) {
          if (service.handler != nullptr) return Status::kAlreadyExists;
          service.handler = handler;
          service.handler_state = state;
          service.executor = &executor_;
        } else if (clients_.emplace(Key{descriptor.instance_name, descriptor.name}, &service)
                       .second) {
          ++service.clients;
        }
        return Status::kOk;
      } catch (const std::bad_alloc&) {
        return Status::kCapacityExceeded;
      } catch (...) {
        return Status::kInternal;
      }
    }
    Impl& owner_;
    const ModuleConfig& config_;
    ThreadExecutor<0>& executor_;
    std::map<Key, Service*> clients_;
  };

  static void Invoke(void* state, const ExecutionContext& context) noexcept {
    auto& call = *static_cast<Call*>(state);
    auto& owner = *call.owner;
    {
      const std::lock_guard lock(owner.mutex);
      if (call.completion_done) {
        owner.pending.erase(call.position);
        return;
      }
    }
    Status status = Status::kTimeout;
    std::size_t size{};
    if (call.info.deadline_ns == 0 || context.timestamp_ns() < call.info.deadline_ns) {
      const auto native = ToAbiExecutionContext(context);
      try {
        status = FromAbiStatus(call.service->handler(
            call.service->handler_state, reinterpret_cast<const std::uint8_t*>(call.request.data()),
            call.request.size(), reinterpret_cast<std::uint8_t*>(call.response.data()),
            call.response.size(), &size, &call.info, &native));
        if (size > call.response.size()) status = Status::kProtocolError;
      } catch (...) {
        status = Status::kInternal;
      }
    }
    std::uint64_t now{};
    const auto clock_status = owner.clock.NowNs(now);
    if (!IsOk(clock_status))
      status = clock_status;
    else if (call.info.deadline_ns != 0 && now >= call.info.deadline_ns)
      status = Status::kTimeout;
    const std::lock_guard lock(owner.mutex);
    call.server_done = true;
    if (call.completion_done) {
      owner.pending.erase(call.position);
      return;
    }
    call.status = status;
    call.response_size = IsOk(status) ? size : 0;
    // A timer may already have removed this reservation and be waiting for
    // this lock; it will observe server_done and complete exactly once.
    call.caller->ScheduleReserved(call.ticket, IsOk(clock_status) ? now : context.timestamp_ns());
  }

  static void Complete(void* state, const ExecutionContext& context) noexcept {
    auto& call = *static_cast<Call*>(state);
    auto& owner = *call.owner;
    Status status;
    std::size_t size{};
    {
      const std::lock_guard lock(owner.mutex);
      status = call.server_done ? call.status : Status::kTimeout;
      if (call.info.deadline_ns != 0 && context.timestamp_ns() >= call.info.deadline_ns)
        status = Status::kTimeout;
      if (IsOk(status)) size = call.response_size;
    }
    const auto native = ToAbiExecutionContext(context);
    try {
      call.completion(
          call.completion_state, ToAbiStatus(status),
          size == 0 ? nullptr : reinterpret_cast<const std::uint8_t*>(call.response.data()), size,
          &call.info, &native);
    } catch (...) {
      std::fputs("AsterCtrl: exception in RPC completion callback\n", stderr);
    }
    const std::lock_guard lock(owner.mutex);
    call.completion_done = true;
    if (call.server_done) owner.pending.erase(call.position);
  }

  explicit Impl(ClockRef value) : clock(value) {}
  ClockRef clock;
  std::mutex mutex;
  std::map<Key, std::unique_ptr<Service>> services;
  std::list<Call> pending;
  std::atomic<bool> sealed{};
  bool accepting{};
  std::uint32_t next_id{};
};

RpcManager::RpcManager(ClockRef clock) : impl_(std::make_unique<Impl>(clock)) {}
RpcManager::~RpcManager() = default;
std::unique_ptr<RpcBackend> RpcManager::CreateEndpoint(const ModuleConfig& config,
                                                       ThreadExecutor<0>& executor) {
  return std::make_unique<Impl::Endpoint>(*impl_, config, executor);
}
Status RpcManager::Seal() noexcept {
  const std::lock_guard lock(impl_->mutex);
  if (impl_->sealed) return Status::kInvalidState;
  if (!impl_->clock) return Status::kUnavailable;
  for (const auto& [key, service] : impl_->services)
    if (service->clients != 0 && service->handler == nullptr) return Status::kUnavailable;
  impl_->sealed = true;
  impl_->accepting = true;
  return Status::kOk;
}
bool RpcManager::sealed() const noexcept { return impl_->sealed; }
void RpcManager::Close() noexcept {
  const std::lock_guard lock(impl_->mutex);
  impl_->accepting = false;
}
void RpcManager::CancelPending() noexcept {
  std::unique_lock lock(impl_->mutex);
  const ExecutionScope scope("shutdown", impl_->clock.NativeHandle());
  const ExecutionContext context("shutdown", ExecutionKind::kThread,
                                 NativeExecutionContext().timestamp_ns());
  const auto native = ToAbiExecutionContext(context);
  while (!impl_->pending.empty()) {
    auto& call = impl_->pending.front();
    if (!call.completion_done) {
      lock.unlock();
      try {
        call.completion(call.completion_state, ASTER_STATUS_CANCELLED, nullptr, 0, &call.info,
                        &native);
      } catch (...) {
        std::fputs("AsterCtrl: exception in RPC cancellation callback\n", stderr);
      }
      lock.lock();
    }
    impl_->pending.pop_front();
  }
}

}  // namespace aster::platform::linux
