/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aster/runtime.hpp"
#include "aster/sim/runtime_services.hpp"
#include "composition.generated.hpp"

int main() {
  static_assert(aster::generated::kTypedComposition);

  aster::sim::ManualClock clock;
  if (!aster::IsOk(clock.Set(42'000U))) {
    return 1;
  }
  const aster::CoreRef core(aster::CoreHandles{
      .configurator = {},
      .logger = {},
      .executor = {},
      .channel = {},
      .rpc = {},
      .parameter = {},
      .clock = aster::ClockRef(clock),
      .allocator = {},
      .hardware = {},
  });
  aster::generated::Composition composition(core);
  aster::Runtime runtime(composition.Modules());
  if (!aster::IsOk(runtime.Initialize()) || !aster::IsOk(runtime.Start())) {
    return 2;
  }
  runtime.Shutdown();
  return runtime.state() == aster::RuntimeState::kStopped ? 0 : 3;
}
