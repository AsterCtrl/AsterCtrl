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

class StateSink final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"state-sink", "examples.StateSink", "demo", {0, 2, 0}};
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
  static aster::Status Receive(void* state, const aster::examples::local::v1::State& message,
                               const aster::MessageInfo&, const aster::ExecutionContext&) noexcept {
    auto& self = *static_cast<StateSink*>(state);
    self.last_sequence_ = message.sequence;
    ++self.received_;
    return aster::Status::kOk;
  }

  aster::Subscriber<aster::examples::local::v1::State> subscriber_;
  std::uint64_t last_sequence_{};
  std::uint32_t received_{};
  bool started_{};
};

}  // namespace examples
