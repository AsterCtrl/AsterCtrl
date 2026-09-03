/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "imu.pb.hpp"

namespace examples {

class StateController final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"state-controller", "examples.StateController", "control", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return subscriber_.Bind(core.channel(), "state", Receive, this);
  }

  aster::Status Start() noexcept override {
    started_ = true;
    return aster::Status::kOk;
  }

  void Shutdown() noexcept override { started_ = false; }

 private:
  static aster::Status Receive(void* state, const aster::examples::can::v1::ImuState& message,
                               const aster::MessageInfo&, const aster::ExecutionContext&) noexcept {
    auto& self = *static_cast<StateController*>(state);
    self.last_sample_us_ = message.timestamp_us;
    ++self.sample_count_;
    return aster::Status::kOk;
  }

  aster::Subscriber<aster::examples::can::v1::ImuState> subscriber_;
  std::uint64_t last_sample_us_{};
  std::uint32_t sample_count_{};
  bool started_{};
};

}  // namespace examples
