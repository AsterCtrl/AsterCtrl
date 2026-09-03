/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "imu.pb.hpp"

namespace examples {

class ImuSource final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"imu-source", "examples.ImuSource", "sensors", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    return publisher_.Bind(core.channel(), "state");
  }

  aster::Status Start() noexcept override {
    aster::examples::can::v1::ImuState state{};
    state.timestamp_us = clock_.NowNs() / 1'000U;
    const auto timestamp = clock_.NowNs();
    return publisher_.Publish(
        state, timestamp,
        aster::ExecutionContext{"control", aster::ExecutionKind::kThread, timestamp});
  }

  void Shutdown() noexcept override {}

 private:
  aster::Publisher<aster::examples::can::v1::ImuState> publisher_;
  aster::ClockRef clock_;
};

}  // namespace examples
