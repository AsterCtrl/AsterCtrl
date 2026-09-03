/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "aster/module.hpp"

namespace examples {

class ClockClient final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"clock-client", "examples.ClockClient", "consumer", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    return clock_ ? aster::Status::kOk : aster::Status::kUnavailable;
  }

  aster::Status Start() noexcept override {
    last_sample_ns_ = clock_.NowNs();
    return aster::Status::kOk;
  }

  void Shutdown() noexcept override { last_sample_ns_ = 0; }

 private:
  aster::ClockRef clock_;
  std::uint64_t last_sample_ns_{};
};

}  // namespace examples
