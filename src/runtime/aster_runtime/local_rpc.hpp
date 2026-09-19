#pragma once

#include "aster_runtime/core/rpc.hpp"

namespace aster {

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
    auto* service = Find(descriptor);
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
    const auto status = executor_.TryPost(WorkItem::Bind<Dispatch>(pending), caller);
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
      const auto abi_info = ToAbiRpcCallInfo(pending.info);
      const auto abi_context = ToAbiExecutionContext(context);
      status = FromAbiStatus(pending.service->handler(
          pending.service->handler_state,
          reinterpret_cast<const std::uint8_t*>(pending.request.data()), pending.request_size,
          reinterpret_cast<std::uint8_t*>(pending.response.data()), pending.response.size(),
          &response_size, &abi_info, &abi_context));
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
    for (std::size_t index = 0; index < response_size; ++index) {
      response[index] = pending.response[index];
    }
    Release(pending);
    const auto abi_info = ToAbiRpcCallInfo(info);
    const auto abi_context = ToAbiExecutionContext(context);
    completion(completion_state, ToAbiStatus(status),
               reinterpret_cast<const std::uint8_t*>(response.data()), response_size, &abi_info,
               &abi_context);
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

  [[nodiscard]] Service* Find(const ServiceDescriptor& descriptor) noexcept {
    for (std::size_t index = 0; index < service_count_; ++index) {
      if (SameServiceIdentity(services_[index].descriptor, descriptor)) {
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
    service = Find(descriptor);
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
