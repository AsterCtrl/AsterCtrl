#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "aster/status.hpp"
#include "aster/transport/channel_bridge.hpp"
#include "aster/transport/router.hpp"
#include "aster/transport/transport.hpp"

namespace aster::transport {

struct ChannelTransportModuleStats {
  std::uint32_t polls{};
  std::uint32_t poll_failures{};
  std::uint32_t schedule_failures{};
  Status last_failure{Status::kOk};
};

template <std::size_t MaxEgressRoutes, std::size_t MaxIngressRoutes>
class ChannelTransportModule final : public Module {
 public:
  static_assert(MaxEgressRoutes + MaxIngressRoutes > 0);

  constexpr ChannelTransportModule(std::string_view name, Transport& transport,
                                   std::uint64_t poll_interval_ns) noexcept
      : name_(name), transport_(transport), poll_interval_ns_(poll_interval_ns) {}

  Status AddEgress(std::uint16_t route_id, const ChannelDescriptor& descriptor,
                   std::uint64_t maximum_age_ns = 0) noexcept {
    if (configuration_sealed_ || !Valid(route_id, descriptor)) {
      return configuration_sealed_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    if (HasRoute(route_id)) {
      return Status::kAlreadyExists;
    }
    if (egress_count_ == egress_config_.size()) {
      return Status::kCapacityExceeded;
    }
    egress_config_[egress_count_++] = {route_id, descriptor, maximum_age_ns};
    return Status::kOk;
  }

  Status AddIngress(std::uint16_t route_id, const ChannelDescriptor& descriptor) noexcept {
    if (configuration_sealed_ || !Valid(route_id, descriptor)) {
      return configuration_sealed_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    if (HasRoute(route_id)) {
      return Status::kAlreadyExists;
    }
    if (ingress_count_ == ingress_config_.size()) {
      return Status::kCapacityExceeded;
    }
    ingress_config_[ingress_count_++] = {route_id, descriptor};
    return Status::kOk;
  }

  [[nodiscard]] ModuleInfo Info() const noexcept override {
    return {name_, "aster.transport.ChannelTransportModule", "asterctrl", {0, 2, 0}};
  }

  Status Initialize(CoreRef core) noexcept override {
    if (configuration_sealed_) {
      return Status::kInvalidState;
    }
    configuration_sealed_ = true;
    executor_ = core.executor();
    clock_ = core.clock();
    const auto channel = core.channel();
    if (name_.empty() || poll_interval_ns_ == 0 || egress_count_ + ingress_count_ == 0 ||
        !executor_ || !clock_ || !channel) {
      return Status::kInvalidArgument;
    }

    for (std::size_t index = 0; index < egress_count_; ++index) {
      const auto& config = egress_config_[index];
      egress_[index].emplace(config.route_id, transport_, config.maximum_age_ns);
      const auto status = egress_[index]->Bind(channel, config.descriptor);
      if (!IsOk(status)) {
        return status;
      }
    }
    for (std::size_t index = 0; index < ingress_count_; ++index) {
      const auto& config = ingress_config_[index];
      ingress_[index].emplace(config.route_id);
      const auto status = ingress_[index]->Bind(channel, router_, config.descriptor);
      if (!IsOk(status)) {
        return status;
      }
    }
    const auto status = router_.Seal();
    initialized_ = IsOk(status);
    return status;
  }

  Status Start() noexcept override {
    if (!initialized_ || running_ || transport_started_) {
      return Status::kInvalidState;
    }
    auto status = transport_.Start(decltype(router_)::Receive, &router_);
    if (!IsOk(status)) {
      return status;
    }
    transport_started_ = true;
    running_ = true;
    const auto now_ns = clock_.NowNs();
    const ExecutionContext caller(name_, ExecutionKind::kThread, now_ns);
    status = Schedule(caller, now_ns);
    if (!IsOk(status)) {
      running_ = false;
      transport_.Stop();
      transport_started_ = false;
    }
    return status;
  }

  void Shutdown() noexcept override {
    running_ = false;
    if (transport_started_) {
      transport_.Stop();
      transport_started_ = false;
    }
  }

  [[nodiscard]] const ChannelTransportModuleStats& stats() const noexcept { return stats_; }

 private:
  struct EgressConfig {
    std::uint16_t route_id{};
    ChannelDescriptor descriptor{};
    std::uint64_t maximum_age_ns{};
  };

  struct IngressConfig {
    std::uint16_t route_id{};
    ChannelDescriptor descriptor{};
  };

  static constexpr std::size_t kRouterCapacity = MaxIngressRoutes == 0 ? 1 : MaxIngressRoutes;

  static constexpr bool Valid(std::uint16_t route_id,
                              const ChannelDescriptor& descriptor) noexcept {
    return route_id != 0 && !descriptor.name.empty() && !descriptor.message_type.name.empty() &&
           descriptor.message_type.max_serialized_size != 0;
  }

  [[nodiscard]] bool HasRoute(std::uint16_t route_id) const noexcept {
    for (std::size_t index = 0; index < egress_count_; ++index) {
      if (egress_config_[index].route_id == route_id) {
        return true;
      }
    }
    for (std::size_t index = 0; index < ingress_count_; ++index) {
      if (ingress_config_[index].route_id == route_id) {
        return true;
      }
    }
    return false;
  }

  static constexpr std::uint64_t SaturatingAdd(std::uint64_t value,
                                               std::uint64_t increment) noexcept {
    return increment > UINT64_MAX - value ? UINT64_MAX : value + increment;
  }

  static void Poll(void* state, const ExecutionContext& caller) noexcept {
    static_cast<ChannelTransportModule*>(state)->PollOnce(caller);
  }

  void PollOnce(const ExecutionContext& caller) noexcept {
    if (!running_) {
      return;
    }
    const auto poll_status = transport_.Poll(caller);
    ++stats_.polls;
    if (!IsOk(poll_status) && poll_status != Status::kUnavailable) {
      ++stats_.poll_failures;
      stats_.last_failure = poll_status;
    }
    if (const auto schedule_status = Schedule(caller, clock_.NowNs()); !IsOk(schedule_status)) {
      ++stats_.schedule_failures;
      stats_.last_failure = schedule_status;
      running_ = false;
    }
  }

  Status Schedule(const ExecutionContext& caller, std::uint64_t now_ns) noexcept {
    return executor_.TryPostAt(SaturatingAdd(now_ns, poll_interval_ns_), {Poll, this}, caller);
  }

  std::string_view name_;
  Transport& transport_;
  std::uint64_t poll_interval_ns_{};
  ExecutorRef executor_{};
  ClockRef clock_{};
  StaticRouter<kRouterCapacity> router_{};
  std::array<EgressConfig, MaxEgressRoutes> egress_config_{};
  std::array<IngressConfig, MaxIngressRoutes> ingress_config_{};
  std::array<std::optional<ChannelPacketEgress>, MaxEgressRoutes> egress_{};
  std::array<std::optional<ChannelPacketIngress>, MaxIngressRoutes> ingress_{};
  std::size_t egress_count_{};
  std::size_t ingress_count_{};
  bool configuration_sealed_{};
  bool initialized_{};
  bool running_{};
  bool transport_started_{};
  ChannelTransportModuleStats stats_{};
};

}  // namespace aster::transport
