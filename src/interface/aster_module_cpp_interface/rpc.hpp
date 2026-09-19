#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster_module_c_interface/rpc/rpc_base.h"
#include "aster_module_cpp_interface/executor.hpp"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/type_support.hpp"

namespace aster {

struct ServiceDescriptor {
  std::string_view name;
  SchemaHash schema_hash;
  TypeDescriptor request_type;
  TypeDescriptor response_type;
  std::string_view instance_name{};
};

[[nodiscard]] constexpr bool SameServiceIdentity(const ServiceDescriptor& left,
                                                 const ServiceDescriptor& right) noexcept {
  return left.name == right.name && left.instance_name == right.instance_name;
}

[[nodiscard]] constexpr bool SameService(const ServiceDescriptor& left,
                                         const ServiceDescriptor& right) noexcept {
  return SameServiceIdentity(left, right) && left.schema_hash == right.schema_hash &&
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

[[nodiscard]] constexpr aster_service_descriptor_t ToAbiServiceDescriptor(
    const ServiceDescriptor& descriptor) noexcept {
  return {
      ToAbiString(descriptor.name),
      ToAbiSchemaHash(descriptor.schema_hash),
      ToAbiTypeDescriptor(descriptor.request_type),
      ToAbiTypeDescriptor(descriptor.response_type),
      ToAbiString(descriptor.instance_name),
  };
}

inline Status FromAbiServiceDescriptor(const aster_service_descriptor_t* descriptor,
                                       ServiceDescriptor& result) noexcept {
  if (descriptor == nullptr) {
    return Status::kInvalidArgument;
  }
  if (descriptor->name.data == nullptr || descriptor->name.size == 0 ||
      (descriptor->instance_name.data == nullptr && descriptor->instance_name.size != 0)) {
    return Status::kInvalidArgument;
  }
  TypeDescriptor request_type{};
  TypeDescriptor response_type{};
  auto status = FromAbiTypeDescriptor(&descriptor->request_type, request_type);
  if (!IsOk(status)) {
    return status;
  }
  status = FromAbiTypeDescriptor(&descriptor->response_type, response_type);
  if (!IsOk(status)) {
    return status;
  }
  result = {
      FromAbiString(descriptor->name),
      FromAbiSchemaHash(descriptor->schema_hash),
      request_type,
      response_type,
      FromAbiString(descriptor->instance_name),
  };
  return Status::kOk;
}

[[nodiscard]] constexpr aster_rpc_call_info_t ToAbiRpcCallInfo(const RpcCallInfo& info) noexcept {
  return {info.request_id, info.deadline_ns};
}

using RawRpcHandler = aster_rpc_handler_t;
using RawRpcCompletion = aster_rpc_completion_t;

class RpcRef {
 public:
  constexpr RpcRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_rpc_base_t*>;
    }
  explicit RpcRef(Backend& value) noexcept : RpcRef(value.NativeHandle()) {}
  constexpr explicit RpcRef(const aster_rpc_base_t* backend) noexcept
      : abi_(backend != nullptr && backend->struct_size >= sizeof(*backend) ? backend : nullptr) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  Status RegisterClient(const ServiceDescriptor& descriptor) const noexcept {
    if (abi_ == nullptr || abi_->register_client == nullptr) {
      return Status::kUnavailable;
    }
    const auto native = ToAbiServiceDescriptor(descriptor);
    return FromAbiStatus(abi_->register_client(abi_->impl, &native));
  }

  Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                        void* handler_state) const noexcept {
    if (abi_ == nullptr || abi_->register_server == nullptr) {
      return Status::kUnavailable;
    }
    const auto native = ToAbiServiceDescriptor(descriptor);
    return FromAbiStatus(abi_->register_server(abi_->impl, &native, handler, handler_state));
  }

  Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                   std::uint64_t deadline, RawRpcCompletion completion, void* state,
                   const ExecutionContext& caller) const noexcept {
    return CallAsync(descriptor, request, deadline, completion, state, &caller);
  }

  Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                   std::uint64_t deadline_ns, RawRpcCompletion completion, void* completion_state,
                   const ExecutionContext* caller = nullptr) const noexcept {
    if (abi_ == nullptr || abi_->call_async == nullptr) {
      return Status::kUnavailable;
    }
    const auto native = ToAbiServiceDescriptor(descriptor);
    const auto context =
        caller == nullptr ? aster_execution_context_t{} : ToAbiExecutionContext(*caller);
    return FromAbiStatus(abi_->call_async(
        abi_->impl, &native, reinterpret_cast<const std::uint8_t*>(request.data()), request.size(),
        deadline_ns, completion, completion_state, caller == nullptr ? nullptr : &context));
  }

  [[nodiscard]] constexpr const aster_rpc_base_t* NativeHandle() const noexcept { return abi_; }

 private:
  const aster_rpc_base_t* abi_{};
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

  static void Dispatch(void* state, aster_status_t status, const std::uint8_t* response_data,
                       std::size_t response_size, const aster_rpc_call_info_t* info,
                       const aster_execution_context_t* context) noexcept {
    if (state == nullptr) {
      return;
    }
    auto& self = *static_cast<RpcCompletion*>(state);
    Response response{};
    auto result = FromAbiStatus(status);
    if (response_data == nullptr && response_size != 0) {
      result = Status::kProtocolError;
    } else if (IsOk(result)) {
      result = TypeSupport<Response>::Decode(
          {reinterpret_cast<const std::byte*>(response_data), response_size}, response);
    }
    const auto callback = self.callback_;
    auto* const callback_state = self.state_;
    self.Cancel();
    if (callback != nullptr && info != nullptr && IsOk(ValidateAbiExecutionContext(context))) {
      const RpcCallInfo native_info{info->request_id, info->deadline_ns};
      callback(callback_state, result, response, native_info, FromAbiExecutionContext(*context));
    }
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

  Status Bind(RpcRef rpc, std::string_view instance_name = {}) noexcept {
    auto descriptor = ServiceTypeSupport<Service>::descriptor();
    descriptor.instance_name = instance_name;
    const auto status = rpc.RegisterClient(descriptor);
    if (IsOk(status)) {
      rpc_ = rpc;
      descriptor_ = descriptor;
    }
    return status;
  }

  Status CallAsync(const Request& request, std::uint64_t deadline, Completion& completion,
                   Callback callback, void* state, const ExecutionContext& caller) const noexcept {
    return CallAsync(request, deadline, completion, callback, state, &caller);
  }

  Status CallAsync(const Request& request, std::uint64_t deadline_ns, Completion& completion,
                   Callback callback, void* callback_state,
                   const ExecutionContext* caller = nullptr) const noexcept {
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
    status =
        rpc_.CallAsync(descriptor_, std::span<const std::byte>(request_bytes.data(), request_size),
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
  ServiceDescriptor descriptor_{};
};

template <ServiceType Service>
class RpcServer {
 public:
  using Request = typename ServiceTypeSupport<Service>::Request;
  using Response = typename ServiceTypeSupport<Service>::Response;
  using Handler = Status (*)(void*, const Request&, Response&, const RpcCallInfo&,
                             const ExecutionContext&) noexcept;

  Status Bind(RpcRef rpc, Handler handler, void* handler_state,
              std::string_view instance_name = {}) noexcept {
    if (handler == nullptr || bound_) {
      return Status::kInvalidArgument;
    }
    handler_ = handler;
    handler_state_ = handler_state;
    auto descriptor = ServiceTypeSupport<Service>::descriptor();
    descriptor.instance_name = instance_name;
    const auto status = rpc.RegisterServer(descriptor, Dispatch, this);
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
  static aster_status_t Dispatch(void* state, const std::uint8_t* request_data,
                                 std::size_t request_size, std::uint8_t* response,
                                 std::size_t response_capacity, std::size_t* response_size,
                                 const aster_rpc_call_info_t* info,
                                 const aster_execution_context_t* caller) noexcept {
    if (response_size == nullptr || (request_data == nullptr && request_size != 0) ||
        (response == nullptr && response_capacity != 0)) {
      return ASTER_STATUS_INVALID_ARGUMENT;
    }
    if (info == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT;
    }
    const auto context_status = ValidateAbiExecutionContext(caller);
    if (!IsOk(context_status)) {
      return ToAbiStatus(context_status);
    }
    *response_size = 0;
    auto& self = *static_cast<RpcServer*>(state);
    Request request{};
    Response response_message{};
    auto status = TypeSupport<Request>::Decode(
        {reinterpret_cast<const std::byte*>(request_data), request_size}, request);
    if (!IsOk(status)) {
      return ToAbiStatus(status);
    }
    const RpcCallInfo native_info{info->request_id, info->deadline_ns};
    status = self.handler_(self.handler_state_, request, response_message, native_info,
                           FromAbiExecutionContext(*caller));
    if (!IsOk(status)) {
      return ToAbiStatus(status);
    }
    status = TypeSupport<Response>::Encode(
        response_message, {reinterpret_cast<std::byte*>(response), response_capacity},
        *response_size);
    return ToAbiStatus(status);
  }

  Handler handler_{};
  void* handler_state_{};
  bool bound_{};
};

}  // namespace aster
