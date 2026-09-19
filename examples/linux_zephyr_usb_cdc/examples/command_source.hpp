/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster_module_cpp_interface/channel.hpp"
#include "aster_module_cpp_interface/module.hpp"
#include "command.pb.hpp"

namespace examples {

class CommandSource final : public aster::ModuleBase {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"command-source", "examples.CommandSource", "gateway", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return publisher_.Bind(core.channel(), "command");
  }

  aster::Status Start() noexcept override {
    aster::examples::usb::v1::Command command{};
    command.sequence = 1;
    if (!command.verb.assign("stop")) {
      return aster::Status::kCapacityExceeded;
    }
    return publisher_.Publish(command);
  }

  void Shutdown() noexcept override {}

 private:
  aster::Publisher<aster::examples::usb::v1::Command> publisher_;
};

}  // namespace examples
