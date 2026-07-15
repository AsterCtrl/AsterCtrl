#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "xrobot/runtime/executor.hpp"
#include "xrobot/runtime/status.hpp"
#include "xrobot/runtime/type_support.hpp"

namespace xrobot::runtime {

struct ServiceDescriptor {
  std::string_view name;
  SchemaHash schema_hash;
};

template <typename Service>
struct ServiceTypeSupport;

template <typename Service>
concept ServiceType =
    requires {
      typename ServiceTypeSupport<Service>::Request;
      typename ServiceTypeSupport<Service>::Response;
      { ServiceTypeSupport<Service>::descriptor() } ->
          std::same_as<ServiceDescriptor>;
    } &&
    MessageType<typename ServiceTypeSupport<Service>::Request> &&
    MessageType<typename ServiceTypeSupport<Service>::Response>;

struct ServiceCallInfo {
  std::uint32_t request_id{};
};

struct ServiceStats {
  std::uint32_t accepted{};
  std::uint32_t completed{};
  std::uint32_t rejected{};
  std::uint32_t schedule_failures{};
  std::size_t high_watermark{};
};

template <ServiceType Service>
using ServiceRequest = typename ServiceTypeSupport<Service>::Request;

template <ServiceType Service>
using ServiceResponse = typename ServiceTypeSupport<Service>::Response;

template <ServiceType Service>
using ServiceCompletion =
    void (*)(void*, Status, const ServiceResponse<Service>&,
             const ServiceCallInfo&, const ExecutionContext&) noexcept;

template <ServiceType Service>
using ServiceHandler =
    Status (*)(void*, const ServiceRequest<Service>&, ServiceResponse<Service>&,
               const ServiceCallInfo&, const ExecutionContext&) noexcept;

template <ServiceType Service>
class ServiceEndpoint {
 public:
  virtual ~ServiceEndpoint() = default;

  virtual Status CallAsync(const ServiceRequest<Service>& request,
                           ServiceCompletion<Service> completion,
                           void* completion_state,
                           const ExecutionContext& caller) noexcept = 0;
};

template <ServiceType Service>
class ServiceServerEndpoint {
 public:
  virtual ~ServiceServerEndpoint() = default;
  virtual Status BindHandler(ServiceHandler<Service> handler,
                             void* handler_state) noexcept = 0;
};

template <ServiceType Service>
class ServiceServer {
 public:
  constexpr ServiceServer() noexcept = default;
  constexpr explicit ServiceServer(
      ServiceServerEndpoint<Service>& endpoint) noexcept
      : endpoint_(&endpoint) {}

  Status BindHandler(ServiceHandler<Service> handler,
                     void* handler_state) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->BindHandler(handler, handler_state);
  }

 private:
  ServiceServerEndpoint<Service>* endpoint_{};
};

template <ServiceType Service>
class ServiceClient {
 public:
  constexpr ServiceClient() noexcept = default;
  constexpr explicit ServiceClient(ServiceEndpoint<Service>& endpoint) noexcept
      : endpoint_(&endpoint) {}

  Status CallAsync(const ServiceRequest<Service>& request,
                   ServiceCompletion<Service> completion,
                   void* completion_state,
                   const ExecutionContext& caller) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->CallAsync(request, completion, completion_state, caller);
  }

  constexpr explicit operator bool() const noexcept {
    return endpoint_ != nullptr;
  }

 private:
  ServiceEndpoint<Service>* endpoint_{};
};

template <ServiceType Service, std::size_t MaxPending>
class StaticService final : public ServiceEndpoint<Service>,
                            public ServiceServerEndpoint<Service> {
 public:
  static_assert(MaxPending > 0);

  using Request = ServiceRequest<Service>;
  using Response = ServiceResponse<Service>;
  using Completion = ServiceCompletion<Service>;
  using Handler = ServiceHandler<Service>;

  StaticService(std::string_view name, Executor& executor) noexcept
      : name_(name), executor_(executor) {
    for (auto& slot : slots_) {
      slot.owner = this;
    }
  }

  StaticService(std::string_view name, Executor& executor, Handler handler,
                void* handler_state) noexcept
      : StaticService(name, executor) {
    handler_ = handler;
    handler_state_ = handler_state;
  }

  Status BindHandler(Handler handler, void* handler_state) noexcept override {
    if (handler == nullptr) {
      return Status::kInvalidArgument;
    }
    if (handler_ != nullptr || pending_ != 0) {
      return Status::kInvalidState;
    }
    handler_ = handler;
    handler_state_ = handler_state;
    return Status::kOk;
  }

  Status CallAsync(const Request& request, Completion completion,
                   void* completion_state,
                   const ExecutionContext& caller) noexcept override {
    if (name_.empty() || handler_ == nullptr || completion == nullptr) {
      ++stats_.rejected;
      return Status::kInvalidArgument;
    }

    Slot* available = nullptr;
    for (auto& slot : slots_) {
      if (!slot.in_use) {
        available = &slot;
        break;
      }
    }
    if (available == nullptr) {
      ++stats_.rejected;
      return Status::kCapacityExceeded;
    }

    available->in_use = true;
    available->request = request;
    available->response = {};
    available->completion = completion;
    available->completion_state = completion_state;
    available->info = ServiceCallInfo{++next_request_id_};
    ++pending_;
    if (pending_ > stats_.high_watermark) {
      stats_.high_watermark = pending_;
    }

    const auto status =
        executor_.TryPost({HandleThunk, available}, caller);
    if (!IsOk(status)) {
      ++stats_.rejected;
      ++stats_.schedule_failures;
      Release(*available);
      return status;
    }

    ++stats_.accepted;
    return Status::kOk;
  }

  ServiceClient<Service> client() noexcept {
    return ServiceClient<Service>(*this);
  }
  ServiceServer<Service> server() noexcept {
    return ServiceServer<Service>(*this);
  }
  std::string_view name() const noexcept { return name_; }
  std::size_t pending() const noexcept { return pending_; }
  const ServiceStats& stats() const noexcept { return stats_; }

 private:
  struct Slot {
    StaticService* owner{};
    Request request{};
    Response response{};
    Completion completion{};
    void* completion_state{};
    ServiceCallInfo info{};
    bool in_use{};
  };

  static void HandleThunk(void* state,
                          const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<Slot*>(state);
    slot.owner->Handle(slot, context);
  }

  void Handle(Slot& slot, const ExecutionContext& context) noexcept {
    const auto status = handler_(handler_state_, slot.request, slot.response,
                                 slot.info, context);
    const auto response = slot.response;
    const auto completion = slot.completion;
    auto* const completion_state = slot.completion_state;
    const auto info = slot.info;
    Release(slot);
    ++stats_.completed;
    completion(completion_state, status, response, info, context);
  }

  void Release(Slot& slot) noexcept {
    slot.in_use = false;
    slot.completion = nullptr;
    slot.completion_state = nullptr;
    --pending_;
  }

  std::string_view name_;
  Executor& executor_;
  Handler handler_{};
  void* handler_state_{};
  std::array<Slot, MaxPending> slots_{};
  std::size_t pending_{};
  std::uint32_t next_request_id_{};
  ServiceStats stats_{};
};

}  // namespace xrobot::runtime
