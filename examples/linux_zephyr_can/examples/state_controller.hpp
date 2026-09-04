/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "aster/rpc.hpp"
#include "imu.pb.hpp"

namespace examples {

class StateController final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"state-controller", "examples.StateController", "control", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    auto status = subscriber_.Bind(core.channel(), "state", Receive, this);
    if (!aster::IsOk(status)) {
      return status;
    }
    return calibration_.Bind(core.rpc());
  }

  aster::Status Start() noexcept override {
    started_ = true;
    return aster::Status::kOk;
  }

  void Shutdown() noexcept override { started_ = false; }

 private:
  static aster::Status Receive(void* state, const aster::examples::can::v1::ImuState& message,
                               const aster::MessageInfo&,
                               const aster::ExecutionContext& context) noexcept {
    auto& self = *static_cast<StateController*>(state);
    self.last_sample_us_ = message.timestamp_us;
    ++self.sample_count_;
    if (!self.calibration_requested_) {
      self.calibration_requested_ = true;
      const auto status = self.calibration_.CallAsync({1U}, context.timestamp_ns() + 100'000'000U,
                                                      self.calibration_completion_,
                                                      CalibrationComplete, &self, context);
      if (!aster::IsOk(status)) {
        self.calibration_requested_ = false;
        return status;
      }
    }
    return aster::Status::kOk;
  }

  static void CalibrationComplete(void* state, aster::Status status,
                                  const aster::examples::can::v1::CalibrationResponse& response,
                                  const aster::RpcCallInfo&,
                                  const aster::ExecutionContext&) noexcept {
    auto& self = *static_cast<StateController*>(state);
    self.calibration_status_ = status;
    if (aster::IsOk(status) && response.valid) {
      self.calibration_revision_ = response.revision;
    }
  }

  aster::Subscriber<aster::examples::can::v1::ImuState> subscriber_;
  aster::RpcClient<aster::examples::can::v1::Sensor::ReadCalibration> calibration_;
  aster::RpcCompletion<aster::examples::can::v1::Sensor::ReadCalibration> calibration_completion_;
  std::uint64_t last_sample_us_{};
  std::uint32_t sample_count_{};
  std::uint32_t calibration_revision_{};
  aster::Status calibration_status_{aster::Status::kUnavailable};
  bool calibration_requested_{};
  bool started_{};
};

}  // namespace examples
