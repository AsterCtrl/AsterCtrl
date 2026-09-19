/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "aster_module_cpp_interface/module.hpp"

namespace examples {

class HardwareClock final : public aster::ModuleBase {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"clock-provider", "examples.HardwareClock", "clock", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    clock_ = core.clock();
    if (!clock_) {
      return aster::Status::kUnavailable;
    }
    aster::ClockDomain domain{};
    const auto status = clock_.GetDomain(domain);
    if (!aster::IsOk(status)) return status;
    return domain == aster::ClockDomain::kSimulated ? aster::Status::kTypeMismatch
                                                    : aster::Status::kOk;
  }

  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}

 private:
  aster::ClockRef clock_;
};

}  // namespace examples
