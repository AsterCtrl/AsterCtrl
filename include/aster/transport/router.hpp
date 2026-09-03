#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "aster/status.hpp"
#include "aster/transport/transport.hpp"

namespace aster::transport {

using RouteHandler = Status (*)(void*, const PacketView&, const ExecutionContext&) noexcept;

template <std::size_t MaxRoutes>
class StaticRouter {
 public:
  static_assert(MaxRoutes > 0);

  Status Register(std::uint16_t route_id, PacketKind kind, const SchemaHash& schema_hash,
                  RouteHandler handler, void* handler_state) noexcept {
    if (sealed_ || route_id == 0 || handler == nullptr) {
      return sealed_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < route_count_; ++index) {
      if (routes_[index].route_id == route_id && routes_[index].kind == kind) {
        return Status::kAlreadyExists;
      }
    }
    if (route_count_ == routes_.size()) {
      return Status::kCapacityExceeded;
    }
    routes_[route_count_++] = {route_id, kind, schema_hash, handler, handler_state};
    return Status::kOk;
  }

  Status Seal() noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  Status Accept(const PacketView& packet, const ExecutionContext& caller) noexcept {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < route_count_; ++index) {
      const auto& route = routes_[index];
      if (route.route_id != packet.header.route_id || route.kind != packet.header.kind) {
        continue;
      }
      if (route.schema_hash != packet.header.schema_hash) {
        return Status::kTypeMismatch;
      }
      return route.handler(route.handler_state, packet, caller);
    }
    return Status::kNotFound;
  }

  static Status Receive(void* state, const PacketView& packet,
                        const ExecutionContext& caller) noexcept {
    if (state == nullptr) {
      return Status::kInvalidArgument;
    }
    return static_cast<StaticRouter*>(state)->Accept(packet, caller);
  }

  [[nodiscard]] constexpr std::size_t route_count() const noexcept { return route_count_; }
  [[nodiscard]] constexpr bool sealed() const noexcept { return sealed_; }

 private:
  struct Route {
    std::uint16_t route_id{};
    PacketKind kind{PacketKind::kChannel};
    SchemaHash schema_hash{};
    RouteHandler handler{};
    void* handler_state{};
  };

  std::array<Route, MaxRoutes> routes_{};
  std::size_t route_count_{};
  bool sealed_{};
};

}  // namespace aster::transport
