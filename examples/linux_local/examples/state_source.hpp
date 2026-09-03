/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "state.pb.hpp"

namespace examples {

class StateSource final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"state-source", "examples.StateSource", "demo", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    return publisher_.Bind(core.channel(), "state");
  }

  aster::Status Start() noexcept override {
    aster::examples::local::v1::State state{};
    state.sequence = 1;
    if (!state.label.assign("ready")) {
      return aster::Status::kCapacityExceeded;
    }
    const auto timestamp = clock_.NowNs();
    return publisher_.Publish(
        state, timestamp,
        aster::ExecutionContext{"main", aster::ExecutionKind::kThread, timestamp});
  }

  void Shutdown() noexcept override {}

 private:
  aster::Publisher<aster::examples::local::v1::State> publisher_;
  aster::ClockRef clock_;
};

}  // namespace examples
