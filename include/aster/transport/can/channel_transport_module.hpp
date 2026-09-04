#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "aster/status.hpp"
#include "aster/transport/can/channel_bridge.hpp"
#include "aster/transport/can/link_control.hpp"
#include "aster/transport/can/reliable_channel_bridge.hpp"

namespace aster::transport::can {

enum class ChannelReliability : std::uint8_t {
  kBestEffort,
  kReliable,
};

struct CanChannelTransportModuleStats {
  std::uint32_t polls{};
  std::uint32_t poll_failures{};
  std::uint32_t schedule_failures{};
  std::uint32_t peer_resets{};
  Status last_failure{Status::kOk};
};

template <typename Adapter, std::size_t MaxEgressRoutes, std::size_t MaxIngressRoutes,
          std::size_t MaximumMessageSize>
class CanChannelTransportModule final : public Module {
 public:
  static_assert(MaxEgressRoutes + MaxIngressRoutes > 0);
  static_assert(MaximumMessageSize > 0);
  static_assert(MaximumMessageSize + 2U <= 96);

  CanChannelTransportModule(std::string_view name, Adapter& adapter, CanLinkControlConfig control,
                            std::uint64_t poll_interval_ns,
                            std::uint64_t retry_timeout_ns = 5'000'000,
                            std::uint8_t maximum_retries = 2,
                            std::uint64_t reassembly_timeout_ns = 100'000'000) noexcept
      : name_(name),
        adapter_(adapter),
        control_(control, adapter.writer(), {ReadClock, this}),
        poll_interval_ns_(poll_interval_ns),
        retry_timeout_ns_(retry_timeout_ns),
        reassembly_timeout_ns_(reassembly_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status AddEgress(std::uint16_t route_id, const ChannelDescriptor& descriptor,
                   ChannelReliability reliability, CanPriority priority = CanPriority::kState,
                   std::uint64_t minimum_period_ns = 0) noexcept {
    if (configuration_sealed_ || !Valid(route_id, descriptor)) {
      return configuration_sealed_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    if (HasRoute(route_id)) {
      return Status::kAlreadyExists;
    }
    if (egress_count_ == egress_config_.size()) {
      return Status::kCapacityExceeded;
    }
    egress_config_[egress_count_++] = {route_id, descriptor, reliability, priority,
                                       minimum_period_ns};
    return Status::kOk;
  }

  Status AddIngress(std::uint16_t route_id, const ChannelDescriptor& descriptor,
                    ChannelReliability reliability, FreshnessConfig freshness = {}) noexcept {
    if (configuration_sealed_ || !Valid(route_id, descriptor)) {
      return configuration_sealed_ ? Status::kInvalidState : Status::kInvalidArgument;
    }
    if (HasRoute(route_id)) {
      return Status::kAlreadyExists;
    }
    if (ingress_count_ == ingress_config_.size()) {
      return Status::kCapacityExceeded;
    }
    ingress_config_[ingress_count_++] = {route_id, descriptor, reliability, freshness};
    return Status::kOk;
  }

  [[nodiscard]] ModuleInfo Info() const noexcept override {
    return {name_, "aster.transport.can.ChannelTransportModule", "asterctrl", {0, 2, 0}};
  }

  Status Initialize(CoreRef core) noexcept override {
    if (configuration_sealed_) {
      return Status::kInvalidState;
    }
    configuration_sealed_ = true;
    executor_ = core.executor();
    clock_ = core.clock();
    const auto channel = core.channel();
    if (name_.empty() || poll_interval_ns_ == 0 || retry_timeout_ns_ == 0 ||
        reassembly_timeout_ns_ == 0 || egress_count_ + ingress_count_ == 0 || !executor_ ||
        !clock_ || !channel) {
      return Status::kInvalidArgument;
    }

    for (std::size_t index = 0; index < egress_count_; ++index) {
      const auto& config = egress_config_[index];
      Status status{};
      if (config.reliability == ChannelReliability::kReliable) {
        egress_[index].reliable.emplace(config.route_id, config.priority,
                                        control_.application_writer(), control_.time_converter(),
                                        config.minimum_period_ns, retry_timeout_ns_,
                                        maximum_retries_);
        status = egress_[index].reliable->Bind(channel, config.descriptor);
      } else {
        egress_[index].fast.emplace(config.route_id, config.priority, control_.application_writer(),
                                    control_.time_converter(), config.minimum_period_ns);
        status = egress_[index].fast->Bind(channel, config.descriptor);
      }
      if (!IsOk(status)) {
        return status;
      }
    }
    for (std::size_t index = 0; index < ingress_count_; ++index) {
      const auto& config = ingress_config_[index];
      Status status{};
      if (config.reliability == ChannelReliability::kReliable) {
        ingress_[index].reliable.emplace(config.route_id, control_.application_writer(),
                                         config.freshness, reassembly_timeout_ns_);
        status = ingress_[index].reliable->Bind(channel, config.descriptor);
      } else {
        ingress_[index].fast.emplace(config.route_id, config.freshness);
        status = ingress_[index].fast->Bind(channel, config.descriptor);
      }
      if (!IsOk(status)) {
        return status;
      }
    }
    initialized_ = true;
    return Status::kOk;
  }

  Status Start() noexcept override {
    if (!initialized_ || running_) {
      return Status::kInvalidState;
    }
    auto status = adapter_.Ready();
    if (!IsOk(status)) {
      return status;
    }
    status = adapter_.Start({Receive, this});
    if (!IsOk(status)) {
      return status;
    }
    adapter_started_ = true;
    running_ = true;
    const auto now_ns = clock_.NowNs();
    status = executor_.TryPostAt(now_ns, {Poll, this}, {name_, ExecutionKind::kThread, now_ns});
    if (!IsOk(status)) {
      running_ = false;
      const auto stop_status = adapter_.Stop();
      if (IsOk(stop_status)) {
        adapter_started_ = false;
      } else {
        stats_.last_failure = stop_status;
      }
    }
    return status;
  }

  void Shutdown() noexcept override {
    running_ = false;
    if (adapter_started_) {
      const auto status = adapter_.Stop();
      if (!IsOk(status)) {
        stats_.last_failure = status;
      } else {
        adapter_started_ = false;
      }
    }
  }

  [[nodiscard]] const CanChannelTransportModuleStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const CanLinkControlPlane& control() const noexcept { return control_; }

 private:
  struct EgressConfig {
    std::uint16_t route_id{};
    ChannelDescriptor descriptor{};
    ChannelReliability reliability{ChannelReliability::kBestEffort};
    CanPriority priority{CanPriority::kState};
    std::uint64_t minimum_period_ns{};
  };

  struct IngressConfig {
    std::uint16_t route_id{};
    ChannelDescriptor descriptor{};
    ChannelReliability reliability{ChannelReliability::kBestEffort};
    FreshnessConfig freshness{};
  };

  struct EgressRoute {
    std::optional<FastChannelEgress<MaximumMessageSize>> fast;
    std::optional<ReliableChannelEgress<MaximumMessageSize>> reliable;
  };

  struct IngressRoute {
    std::optional<FastChannelIngress<MaximumMessageSize>> fast;
    std::optional<ReliableChannelIngress<MaximumMessageSize>> reliable;
  };

  static constexpr bool Valid(std::uint16_t route_id,
                              const ChannelDescriptor& descriptor) noexcept {
    return route_id >= kFirstApplicationRouteId && route_id <= kMaximumRouteId &&
           !descriptor.name.empty() && !descriptor.message_type.name.empty() &&
           descriptor.message_type.max_serialized_size != 0 &&
           descriptor.message_type.max_serialized_size <= MaximumMessageSize;
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

  static std::uint64_t ReadClock(void* state) noexcept {
    return static_cast<CanChannelTransportModule*>(state)->clock_.NowNs();
  }

  static Status Receive(void* state, const CanFrame& frame, std::uint64_t receive_time_ns,
                        const ExecutionContext& caller) noexcept {
    return static_cast<CanChannelTransportModule*>(state)->Accept(frame, receive_time_ns, caller);
  }

  Status Accept(const CanFrame& frame, std::uint64_t receive_time_ns,
                const ExecutionContext& caller) noexcept {
    const auto id = CanArbitrationId::Decode(frame.arbitration_id);
    if (!id.has_value() || frame.size == 0) {
      return Status::kInvalidArgument;
    }
    if (id->route_id < kFirstApplicationRouteId) {
      return control_.Accept(frame, receive_time_ns, caller);
    }
    if (!control_.application_enabled()) {
      return Status::kUnavailable;
    }
    const auto receive_network_time_ns = control_.ToNetworkTime(receive_time_ns);
    for (std::size_t index = 0; index < egress_count_; ++index) {
      if (egress_config_[index].route_id == id->route_id && egress_[index].reliable.has_value()) {
        return egress_[index].reliable->AcceptAcknowledgement(frame);
      }
    }
    for (std::size_t index = 0; index < ingress_count_; ++index) {
      if (ingress_config_[index].route_id != id->route_id) {
        continue;
      }
      return ingress_[index].reliable.has_value()
                 ? ingress_[index].reliable->Accept(frame, receive_network_time_ns, caller)
                 : ingress_[index].fast->Accept(frame, receive_network_time_ns, caller);
    }
    return Status::kUnavailable;
  }

  static void Poll(void* state, const ExecutionContext& caller) noexcept {
    static_cast<CanChannelTransportModule*>(state)->PollOnce(caller);
  }

  void PollOnce(const ExecutionContext& caller) noexcept {
    if (!running_) {
      return;
    }
    const auto adapter_status = adapter_.Poll(caller);
    RecordPoll(adapter_status);

    const auto now_ns = clock_.NowNs();
    const auto control_status = control_.Poll(now_ns, caller);
    RecordPoll(control_status);
    const auto peer_restarts = control_.stats().peer_restarts;
    if (peer_restarts != observed_peer_restarts_) {
      observed_peer_restarts_ = peer_restarts;
      ResetPeerRoutes();
      ++stats_.peer_resets;
    }

    const auto network_now_ns = control_.ToNetworkTime(now_ns);
    for (std::size_t index = 0; index < egress_count_; ++index) {
      if (egress_[index].reliable.has_value()) {
        RecordPoll(egress_[index].reliable->Poll(network_now_ns, caller));
      }
    }
    for (std::size_t index = 0; index < ingress_count_; ++index) {
      if (ingress_[index].reliable.has_value()) {
        RecordPoll(ingress_[index].reliable->Poll(network_now_ns));
      } else {
        ingress_[index].fast->freshness().Poll(network_now_ns);
      }
    }
    ++stats_.polls;

    const auto status =
        executor_.TryPostAt(SaturatingAdd(now_ns, poll_interval_ns_), {Poll, this}, caller);
    if (!IsOk(status)) {
      ++stats_.schedule_failures;
      stats_.last_failure = status;
      running_ = false;
      static_cast<void>(adapter_.Stop());
      adapter_started_ = false;
    }
  }

  void RecordPoll(Status status) noexcept {
    if (!IsOk(status) && status != Status::kUnavailable && status != Status::kTimeout &&
        status != Status::kCapacityExceeded) {
      ++stats_.poll_failures;
      stats_.last_failure = status;
    }
  }

  void ResetPeerRoutes() noexcept {
    for (std::size_t index = 0; index < egress_count_; ++index) {
      if (egress_[index].reliable.has_value()) {
        egress_[index].reliable->ResetPeer();
      }
    }
    for (std::size_t index = 0; index < ingress_count_; ++index) {
      if (ingress_[index].reliable.has_value()) {
        ingress_[index].reliable->ResetPeer();
      } else {
        ingress_[index].fast->ResetPeer();
      }
    }
  }

  static constexpr std::uint64_t SaturatingAdd(std::uint64_t value,
                                               std::uint64_t increment) noexcept {
    return increment > UINT64_MAX - value ? UINT64_MAX : value + increment;
  }

  std::string_view name_;
  Adapter& adapter_;
  CanLinkControlPlane control_;
  std::uint64_t poll_interval_ns_{};
  std::uint64_t retry_timeout_ns_{};
  std::uint64_t reassembly_timeout_ns_{};
  std::uint8_t maximum_retries_{};
  ExecutorRef executor_{};
  ClockRef clock_{};
  std::array<EgressConfig, MaxEgressRoutes> egress_config_{};
  std::array<IngressConfig, MaxIngressRoutes> ingress_config_{};
  std::array<EgressRoute, MaxEgressRoutes> egress_{};
  std::array<IngressRoute, MaxIngressRoutes> ingress_{};
  std::size_t egress_count_{};
  std::size_t ingress_count_{};
  std::uint32_t observed_peer_restarts_{};
  bool configuration_sealed_{};
  bool initialized_{};
  bool adapter_started_{};
  bool running_{};
  CanChannelTransportModuleStats stats_{};
};

}  // namespace aster::transport::can
