#pragma once

#include <array>
#include <cstddef>

#include "aster/rpc.hpp"

namespace aster {

template <std::size_t MaxRemoteClients>
class RpcRouter final : public RpcBackend {
 public:
  static_assert(MaxRemoteClients > 0);

  constexpr explicit RpcRouter(RpcBackend& local) noexcept : local_(&local) {}

  Status AddRemoteClient(const ServiceDescriptor& descriptor, RpcBackend& backend) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (!Valid(descriptor) || &backend == local_) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < route_count_; ++index) {
      if (routes_[index].descriptor.name == descriptor.name) {
        return SameService(routes_[index].descriptor, descriptor) ? Status::kAlreadyExists
                                                                  : Status::kTypeMismatch;
      }
    }
    if (route_count_ == routes_.size()) {
      return Status::kCapacityExceeded;
    }
    routes_[route_count_++] = {descriptor, &backend};
    return Status::kOk;
  }

  Status RegisterClient(const ServiceDescriptor& descriptor) noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    auto* const backend = ClientBackend(descriptor);
    return backend == nullptr ? Status::kTypeMismatch : backend->RegisterClient(descriptor);
  }

  Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                        void* handler_state) noexcept override {
    return sealed_ ? Status::kInvalidState
                   : local_->RegisterServer(descriptor, handler, handler_state);
  }

  Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                   std::uint64_t deadline_ns, RawRpcCompletion completion, void* completion_state,
                   const ExecutionContext& caller) noexcept override {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    auto* const backend = ClientBackend(descriptor);
    return backend == nullptr ? Status::kTypeMismatch
                              : backend->CallAsync(descriptor, request, deadline_ns, completion,
                                                   completion_state, caller);
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    auto status = local_->Seal();
    if (!IsOk(status)) {
      return status;
    }
    for (std::size_t index = 0; index < route_count_; ++index) {
      status = routes_[index].backend->Seal();
      if (!IsOk(status)) {
        return status;
      }
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }
  [[nodiscard]] std::size_t remote_client_count() const noexcept { return route_count_; }

 private:
  struct Route {
    ServiceDescriptor descriptor{};
    RpcBackend* backend{};
  };

  static constexpr bool Valid(const ServiceDescriptor& descriptor) noexcept {
    return !descriptor.name.empty() && !descriptor.request_type.name.empty() &&
           !descriptor.response_type.name.empty() &&
           descriptor.request_type.max_serialized_size != 0 &&
           descriptor.response_type.max_serialized_size != 0;
  }

  [[nodiscard]] RpcBackend* ClientBackend(const ServiceDescriptor& descriptor) const noexcept {
    for (std::size_t index = 0; index < route_count_; ++index) {
      if (routes_[index].descriptor.name != descriptor.name) {
        continue;
      }
      return SameService(routes_[index].descriptor, descriptor) ? routes_[index].backend : nullptr;
    }
    return local_;
  }

  RpcBackend* local_{};
  std::array<Route, MaxRemoteClients> routes_{};
  std::size_t route_count_{};
  bool sealed_{};
};

}  // namespace aster
