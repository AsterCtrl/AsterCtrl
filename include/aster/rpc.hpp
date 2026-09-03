#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/executor.hpp"
#include "aster/registry.hpp"
#include "aster/status.hpp"
#include "aster/type_support.hpp"

namespace aster {

struct ServiceDescriptor {
  std::string_view name;
  SchemaHash schema_hash;
  TypeDescriptor request_type;
  TypeDescriptor response_type;
};

[[nodiscard]] constexpr bool SameService(const ServiceDescriptor& left,
                                         const ServiceDescriptor& right) noexcept {
  return left.name == right.name && left.schema_hash == right.schema_hash &&
         SameType(left.request_type, right.request_type) &&
         SameType(left.response_type, right.response_type);
}

template <typename Service>
struct ServiceTypeSupport;

template <typename Service>
concept ServiceType =
    requires {
      typename ServiceTypeSupport<Service>::Request;
      typename ServiceTypeSupport<Service>::Response;
      { ServiceTypeSupport<Service>::descriptor() } -> std::same_as<ServiceDescriptor>;
    } && MessageType<typename ServiceTypeSupport<Service>::Request> &&
    MessageType<typename ServiceTypeSupport<Service>::Response>;

struct RpcCallInfo {
  std::uint32_t request_id{};
  std::uint64_t deadline_ns{};
};

using RawRpcHandler = Status (*)(void*, std::span<const std::byte>, std::span<std::byte>,
                                 std::size_t&, const RpcCallInfo&,
                                 const ExecutionContext&) noexcept;
using RawRpcCompletion = void (*)(void*, Status, std::span<const std::byte>, const RpcCallInfo&,
                                  const ExecutionContext&) noexcept;

class RpcBackend : public Registry {
 public:
  virtual Status RegisterClient(const ServiceDescriptor& descriptor) noexcept = 0;
  virtual Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                                void* handler_state) noexcept = 0;
  virtual Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                           std::uint64_t deadline_ns, RawRpcCompletion completion,
                           void* completion_state, const ExecutionContext& caller) noexcept = 0;
};

class RpcRef {
 public:
  constexpr RpcRef() noexcept = default;
  constexpr explicit RpcRef(RpcBackend& backend) noexcept : backend_(&backend) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return backend_ != nullptr; }

  Status RegisterClient(const ServiceDescriptor& descriptor) const noexcept {
    return backend_ == nullptr ? Status::kUnavailable : backend_->RegisterClient(descriptor);
  }

  Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                        void* handler_state) const noexcept {
    return backend_ == nullptr ? Status::kUnavailable
                               : backend_->RegisterServer(descriptor, handler, handler_state);
  }

  Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                   std::uint64_t deadline_ns, RawRpcCompletion completion, void* completion_state,
                   const ExecutionContext& caller) const noexcept {
    return backend_ == nullptr ? Status::kUnavailable
                               : backend_->CallAsync(descriptor, request, deadline_ns, completion,
                                                     completion_state, caller);
  }

 private:
  RpcBackend* backend_{};
};

template <ServiceType Service>
class RpcClient;

template <ServiceType Service>
class RpcCompletion {
 public:
  using Response = typename ServiceTypeSupport<Service>::Response;
  using Callback = void (*)(void*, Status, const Response&, const RpcCallInfo&,
                            const ExecutionContext&) noexcept;

  constexpr RpcCompletion() noexcept = default;
  RpcCompletion(const RpcCompletion&) = delete;
  RpcCompletion& operator=(const RpcCompletion&) = delete;

  [[nodiscard]] bool pending() const noexcept { return pending_.load(std::memory_order_acquire); }

 private:
  friend class RpcClient<Service>;

  Status Prepare(Callback callback, void* state) noexcept {
    if (callback == nullptr) {
      return Status::kInvalidArgument;
    }
    bool expected = false;
    if (!pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      return Status::kInvalidState;
    }
    callback_ = callback;
    state_ = state;
    return Status::kOk;
  }

  void Cancel() noexcept {
    callback_ = nullptr;
    state_ = nullptr;
    pending_.store(false, std::memory_order_release);
  }

  static void Dispatch(void* state, Status status, std::span<const std::byte> response_bytes,
                       const RpcCallInfo& info, const ExecutionContext& context) noexcept {
    auto& self = *static_cast<RpcCompletion*>(state);
    Response response{};
    auto result = status;
    if (IsOk(result)) {
      result = TypeSupport<Response>::Decode(response_bytes, response);
    }
    const auto callback = self.callback_;
    auto* const callback_state = self.state_;
    self.Cancel();
    callback(callback_state, result, response, info, context);
  }

  Callback callback_{};
  void* state_{};
  std::atomic<bool> pending_{};
};

template <ServiceType Service>
class RpcClient {
 public:
  using Request = typename ServiceTypeSupport<Service>::Request;
  using Completion = RpcCompletion<Service>;
  using Callback = typename Completion::Callback;

  Status Bind(RpcRef rpc) noexcept {
    const auto status = rpc.RegisterClient(ServiceTypeSupport<Service>::descriptor());
    if (IsOk(status)) {
      rpc_ = rpc;
    }
    return status;
  }

  Status CallAsync(const Request& request, std::uint64_t deadline_ns, Completion& completion,
                   Callback callback, void* callback_state,
                   const ExecutionContext& caller) const noexcept {
    constexpr auto request_capacity = TypeSupport<Request>::descriptor().max_serialized_size;
    static_assert(request_capacity > 0);
    std::array<std::byte, request_capacity> request_bytes{};
    std::size_t request_size{};
    auto status = TypeSupport<Request>::Encode(request, request_bytes, request_size);
    if (!IsOk(status)) {
      return status;
    }
    if (request_size > request_bytes.size()) {
      return Status::kInternal;
    }
    status = completion.Prepare(callback, callback_state);
    if (!IsOk(status)) {
      return status;
    }
    status = rpc_.CallAsync(ServiceTypeSupport<Service>::descriptor(),
                            std::span<const std::byte>(request_bytes.data(), request_size),
                            deadline_ns, Completion::Dispatch, &completion, caller);
    if (!IsOk(status)) {
      completion.Cancel();
    }
    return status;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(rpc_);
  }

 private:
  RpcRef rpc_;
};

template <ServiceType Service>
class RpcServer {
 public:
  using Request = typename ServiceTypeSupport<Service>::Request;
  using Response = typename ServiceTypeSupport<Service>::Response;
  using Handler = Status (*)(void*, const Request&, Response&, const RpcCallInfo&,
                             const ExecutionContext&) noexcept;

  Status Bind(RpcRef rpc, Handler handler, void* handler_state) noexcept {
    if (handler == nullptr || bound_) {
      return Status::kInvalidArgument;
    }
    handler_ = handler;
    handler_state_ = handler_state;
    const auto status =
        rpc.RegisterServer(ServiceTypeSupport<Service>::descriptor(), Dispatch, this);
    if (!IsOk(status)) {
      handler_ = nullptr;
      handler_state_ = nullptr;
      return status;
    }
    bound_ = true;
    return Status::kOk;
  }

  [[nodiscard]] constexpr bool bound() const noexcept { return bound_; }

 private:
  static Status Dispatch(void* state, std::span<const std::byte> request_bytes,
                         std::span<std::byte> response_bytes, std::size_t& response_size,
                         const RpcCallInfo& info, const ExecutionContext& caller) noexcept {
    auto& self = *static_cast<RpcServer*>(state);
    Request request{};
    Response response{};
    auto status = TypeSupport<Request>::Decode(request_bytes, request);
    if (!IsOk(status)) {
      return status;
    }
    status = self.handler_(self.handler_state_, request, response, info, caller);
    if (!IsOk(status)) {
      return status;
    }
    return TypeSupport<Response>::Encode(response, response_bytes, response_size);
  }

  Handler handler_{};
  void* handler_state_{};
  bool bound_{};
};

struct LocalRpcStats {
  std::uint32_t calls{};
  std::uint32_t completed{};
  std::uint32_t failures{};
  std::uint32_t rejected{};
  std::uint32_t schedule_failures{};
  std::uint32_t timeouts{};
  std::size_t pending_high_watermark{};
};

template <std::size_t MaxServices, std::size_t MaximumRequestSize, std::size_t MaximumResponseSize,
          std::size_t MaxPending = MaxServices>
class LocalRpc final : public RpcBackend {
 public:
  static_assert(MaxServices > 0);
  static_assert(MaximumRequestSize > 0);
  static_assert(MaximumResponseSize > 0);
  static_assert(MaxPending > 0);

  constexpr explicit LocalRpc(ExecutorRef executor) noexcept : executor_(executor) {
    for (auto& pending : pending_) {
      pending.owner = this;
    }
  }

  Status RegisterClient(const ServiceDescriptor& descriptor) noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    Service* service{};
    const auto status = FindOrAdd(descriptor, service);
    if (!IsOk(status)) {
      return status;
    }
    if (service->client_count == UINT16_MAX) {
      return Status::kCapacityExceeded;
    }
    ++service->client_count;
    return Status::kOk;
  }

  Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                        void* handler_state) noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (handler == nullptr) {
      return Status::kInvalidArgument;
    }
    Service* service{};
    const auto status = FindOrAdd(descriptor, service);
    if (!IsOk(status)) {
      return status;
    }
    if (service->handler != nullptr) {
      return Status::kAlreadyExists;
    }
    service->handler = handler;
    service->handler_state = handler_state;
    return Status::kOk;
  }

  Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                   std::uint64_t deadline_ns, RawRpcCompletion completion, void* completion_state,
                   const ExecutionContext& caller) noexcept override {
    if (!sealed_) {
      return Reject(Status::kInvalidState);
    }
    if (completion == nullptr) {
      return Reject(Status::kInvalidArgument);
    }
    auto* service = Find(descriptor.name);
    if (service == nullptr) {
      return Reject(Status::kNotFound);
    }
    if (!SameService(service->descriptor, descriptor) ||
        request.size() > descriptor.request_type.max_serialized_size) {
      return Reject(Status::kTypeMismatch);
    }
    if (deadline_ns != 0 && caller.timestamp_ns() >= deadline_ns) {
      return Reject(Status::kTimeout);
    }
    if (service->handler == nullptr) {
      return Reject(Status::kUnavailable);
    }
    auto* pending = Reserve();
    if (pending == nullptr) {
      return Reject(Status::kCapacityExceeded);
    }
    std::copy(request.begin(), request.end(), pending->request.begin());
    pending->request_size = request.size();
    pending->service = service;
    pending->completion = completion;
    pending->completion_state = completion_state;
    pending->info = {NextRequestId(), deadline_ns};
    const auto status = executor_.TryPost({Dispatch, pending}, caller);
    if (!IsOk(status)) {
      Release(*pending);
      schedule_failures_.fetch_add(1, std::memory_order_relaxed);
      return Reject(status);
    }
    calls_.fetch_add(1, std::memory_order_relaxed);
    return Status::kOk;
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (!executor_) {
      return Status::kUnavailable;
    }
    for (std::size_t index = 0; index < service_count_; ++index) {
      if (services_[index].client_count != 0 && services_[index].handler == nullptr) {
        return Status::kUnavailable;
      }
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }
  [[nodiscard]] std::size_t service_count() const noexcept { return service_count_; }
  [[nodiscard]] std::size_t pending_count() const noexcept {
    return pending_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] LocalRpcStats stats() const noexcept {
    return {
        calls_.load(std::memory_order_relaxed),
        completed_.load(std::memory_order_relaxed),
        failures_.load(std::memory_order_relaxed),
        rejected_.load(std::memory_order_relaxed),
        schedule_failures_.load(std::memory_order_relaxed),
        timeouts_.load(std::memory_order_relaxed),
        pending_high_watermark_.load(std::memory_order_relaxed),
    };
  }

 private:
  struct Service {
    ServiceDescriptor descriptor{};
    RawRpcHandler handler{};
    void* handler_state{};
    std::uint16_t client_count{};
  };

  struct PendingCall {
    LocalRpc* owner{};
    Service* service{};
    std::array<std::byte, MaximumRequestSize> request{};
    std::array<std::byte, MaximumResponseSize> response{};
    std::size_t request_size{};
    RpcCallInfo info{};
    RawRpcCompletion completion{};
    void* completion_state{};
    std::atomic<bool> in_use{};
  };

  [[nodiscard]] Status Reject(Status status) noexcept {
    rejected_.fetch_add(1, std::memory_order_relaxed);
    return status;
  }

  [[nodiscard]] PendingCall* Reserve() noexcept {
    for (auto& pending : pending_) {
      bool expected = false;
      if (pending.in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        const auto count = pending_count_.fetch_add(1, std::memory_order_relaxed) + 1U;
        auto high = pending_high_watermark_.load(std::memory_order_relaxed);
        while (high < count && !pending_high_watermark_.compare_exchange_weak(
                                   high, count, std::memory_order_relaxed)) {
        }
        return &pending;
      }
    }
    return nullptr;
  }

  void Release(PendingCall& pending) noexcept {
    pending.service = nullptr;
    pending.request_size = 0;
    pending.info = {};
    pending.completion = nullptr;
    pending.completion_state = nullptr;
    pending_count_.fetch_sub(1, std::memory_order_relaxed);
    pending.in_use.store(false, std::memory_order_release);
  }

  static void Dispatch(void* state, const ExecutionContext& context) noexcept {
    auto& pending = *static_cast<PendingCall*>(state);
    pending.owner->Complete(pending, context);
  }

  void Complete(PendingCall& pending, const ExecutionContext& context) noexcept {
    std::size_t response_size{};
    Status status{};
    if (pending.info.deadline_ns != 0 && context.timestamp_ns() >= pending.info.deadline_ns) {
      status = Status::kTimeout;
      timeouts_.fetch_add(1, std::memory_order_relaxed);
    } else {
      status = pending.service->handler(
          pending.service->handler_state,
          std::span<const std::byte>(pending.request.data(), pending.request_size),
          pending.response, response_size, pending.info, context);
      if (response_size > pending.response.size() ||
          response_size > pending.service->descriptor.response_type.max_serialized_size) {
        status = Status::kInternal;
        response_size = 0;
      } else if (!IsOk(status)) {
        response_size = 0;
      }
    }
    if (!IsOk(status)) {
      failures_.fetch_add(1, std::memory_order_relaxed);
    }
    completed_.fetch_add(1, std::memory_order_relaxed);
    const auto completion = pending.completion;
    auto* const completion_state = pending.completion_state;
    const auto info = pending.info;
    std::array<std::byte, MaximumResponseSize> response{};
    std::copy_n(pending.response.begin(), response_size, response.begin());
    Release(pending);
    completion(completion_state, status, std::span<const std::byte>(response.data(), response_size),
               info, context);
  }

  [[nodiscard]] std::uint32_t NextRequestId() noexcept {
    auto current = next_request_id_.load(std::memory_order_relaxed);
    while (true) {
      const auto next = current == UINT32_MAX ? 1U : current + 1U;
      if (next_request_id_.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
        return next;
      }
    }
  }

  [[nodiscard]] Service* Find(std::string_view name) noexcept {
    for (std::size_t index = 0; index < service_count_; ++index) {
      if (services_[index].descriptor.name == name) {
        return &services_[index];
      }
    }
    return nullptr;
  }

  Status FindOrAdd(const ServiceDescriptor& descriptor, Service*& service) noexcept {
    service = nullptr;
    if (descriptor.name.empty() || descriptor.request_type.name.empty() ||
        descriptor.response_type.name.empty() || descriptor.request_type.max_serialized_size == 0 ||
        descriptor.response_type.max_serialized_size == 0 ||
        descriptor.request_type.max_serialized_size > MaximumRequestSize ||
        descriptor.response_type.max_serialized_size > MaximumResponseSize) {
      return Status::kInvalidArgument;
    }
    service = Find(descriptor.name);
    if (service != nullptr) {
      return SameService(service->descriptor, descriptor) ? Status::kOk : Status::kTypeMismatch;
    }
    if (service_count_ == services_.size()) {
      return Status::kCapacityExceeded;
    }
    service = &services_[service_count_++];
    service->descriptor = descriptor;
    return Status::kOk;
  }

  ExecutorRef executor_;
  std::array<Service, MaxServices> services_{};
  std::array<PendingCall, MaxPending> pending_{};
  std::size_t service_count_{};
  std::atomic<std::size_t> pending_count_{};
  std::atomic<std::uint32_t> next_request_id_{};
  std::atomic<std::uint32_t> calls_{};
  std::atomic<std::uint32_t> completed_{};
  std::atomic<std::uint32_t> failures_{};
  std::atomic<std::uint32_t> rejected_{};
  std::atomic<std::uint32_t> schedule_failures_{};
  std::atomic<std::uint32_t> timeouts_{};
  std::atomic<std::size_t> pending_high_watermark_{};
  bool sealed_{};
};

}  // namespace aster
