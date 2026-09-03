/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster/channel.hpp"
#include "aster/module.hpp"
#include "command.pb.hpp"

namespace examples {

class CommandSource final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"command-source", "examples.CommandSource", "gateway", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    return publisher_.Bind(core.channel(), "command");
  }

  aster::Status Start() noexcept override {
    aster::examples::usb::v1::Command command{};
    command.sequence = 1;
    if (!command.verb.assign("stop")) {
      return aster::Status::kCapacityExceeded;
    }
    const auto timestamp = clock_.NowNs();
    return publisher_.Publish(
        command, timestamp,
        aster::ExecutionContext{"io", aster::ExecutionKind::kThread, timestamp});
  }

  void Shutdown() noexcept override {}

 private:
  aster::Publisher<aster::examples::usb::v1::Command> publisher_;
  aster::ClockRef clock_;
};

}  // namespace examples
