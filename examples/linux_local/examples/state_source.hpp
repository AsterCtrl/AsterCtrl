/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster_module_cpp_interface/channel.hpp"
#include "aster_module_cpp_interface/module.hpp"
#include "state.pb.hpp"

namespace examples {

class StateSource final : public aster::ModuleBase {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"state-source", "examples.StateSource", "demo", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return publisher_.Bind(core.channel(), "state");
  }

  aster::Status Start() noexcept override {
    aster::examples::local::v1::State state{};
    state.sequence = 1;
    if (!state.label.assign("ready")) {
      return aster::Status::kCapacityExceeded;
    }
    return publisher_.Publish(state);
  }

  void Shutdown() noexcept override {}

 private:
  aster::Publisher<aster::examples::local::v1::State> publisher_;
};

}  // namespace examples
