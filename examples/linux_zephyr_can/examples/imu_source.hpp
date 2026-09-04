/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "aster/channel.hpp"
#include "aster/executor.hpp"
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
    executor_ = core.executor();
    if (!clock_ || !executor_) {
      return aster::Status::kUnavailable;
    }
    return publisher_.Bind(core.channel(), "state");
  }

  aster::Status Start() noexcept override {
    running_ = true;
    const auto now = clock_.NowNs();
    const auto status =
        executor_.TryPost({Publish, this}, {"control", aster::ExecutionKind::kThread, now});
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

  void PublishOnce(const aster::ExecutionContext& caller) noexcept {
    if (!running_) {
      return;
    }
    aster::examples::can::v1::ImuState state{};
    state.timestamp_us = clock_.NowNs() / 1'000U;
    const auto timestamp = clock_.NowNs();
    static_cast<void>(publisher_.Publish(
        state, timestamp,
        aster::ExecutionContext{"control", aster::ExecutionKind::kThread, timestamp}));
    static_cast<void>(executor_.TryPostAt(timestamp + 10'000'000U, {Publish, this}, caller));
  }

  aster::Publisher<aster::examples::can::v1::ImuState> publisher_;
  aster::ClockRef clock_;
  aster::ExecutorRef executor_;
  bool running_{};
};

}  // namespace examples
