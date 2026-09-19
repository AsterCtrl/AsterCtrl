/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "aster_module_cpp_interface/channel.hpp"
#include "aster_module_cpp_interface/executor.hpp"
#include "aster_module_cpp_interface/module.hpp"
#include "aster_module_cpp_interface/rpc.hpp"
#include "imu.pb.hpp"

namespace examples {

class ImuSource final : public aster::ModuleBase {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"imu-source", "examples.ImuSource", "sensors", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    executor_ = core.executor();
    if (!clock_ || !executor_) {
      return aster::Status::kUnavailable;
    }
    auto status = publisher_.Bind(core.channel(), "state");
    if (!aster::IsOk(status)) {
      return status;
    }
    return calibration_.Bind(core.rpc(), HandleCalibration, this);
  }

  aster::Status Start() noexcept override {
    running_ = true;
    const auto status = executor_.TryPost(aster::WorkItem::Bind<Publish>(this));
    if (!aster::IsOk(status)) {
      running_ = false;
    }
    return status;
  }

  void Shutdown() noexcept override { running_ = false; }

 private:
  static void Publish(void* state, const aster::ExecutionContext& caller) noexcept {
    static_cast<ImuSource*>(state)->PublishOnce(caller);
  }

  static aster::Status HandleCalibration(
      void*, const aster::examples::can::v1::CalibrationRequest& request,
      aster::examples::can::v1::CalibrationResponse& response, const aster::RpcCallInfo&,
      const aster::ExecutionContext&) noexcept {
    response.revision = request.sensor_id + 1U;
    response.valid = true;
    return aster::Status::kOk;
  }

  void PublishOnce(const aster::ExecutionContext& caller) noexcept {
    if (!running_) {
      return;
    }
    aster::examples::can::v1::ImuState state{};
    std::uint64_t timestamp{};
    if (!aster::IsOk(clock_.NowNs(timestamp))) {
      running_ = false;
      return;
    }
    state.timestamp_us = timestamp / 1'000U;
    static_cast<void>(publisher_.Publish(state, timestamp));
    static_cast<void>(
        executor_.TryPostAt(timestamp + 10'000'000U, aster::WorkItem::Bind<Publish>(this), caller));
  }

  aster::Publisher<aster::examples::can::v1::ImuState> publisher_;
  aster::RpcServer<aster::examples::can::v1::Sensor::ReadCalibration> calibration_;
  aster::ClockRef clock_;
  aster::ExecutorRef executor_;
  bool running_{};
};

}  // namespace examples
