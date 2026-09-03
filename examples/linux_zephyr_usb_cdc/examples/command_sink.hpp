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

class CommandSink final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"command-sink", "examples.CommandSink", "actuator", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return subscriber_.Bind(core.channel(), "command", Receive, this);
  }

  aster::Status Start() noexcept override {
    started_ = true;
    return aster::Status::kOk;
  }

  void Shutdown() noexcept override { started_ = false; }

 private:
  static aster::Status Receive(void* state, const aster::examples::usb::v1::Command& command,
                               const aster::MessageInfo&, const aster::ExecutionContext&) noexcept {
    auto& self = *static_cast<CommandSink*>(state);
    self.last_sequence_ = command.sequence;
    ++self.received_;
    return aster::Status::kOk;
  }

  aster::Subscriber<aster::examples::usb::v1::Command> subscriber_;
  std::uint32_t last_sequence_{};
  std::uint32_t received_{};
  bool started_{};
};

}  // namespace examples
