/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <array>

#include "aster/channel.hpp"
#include "aster/runtime.hpp"
#include "composition.generated.hpp"

int main() {
  static_assert(aster::generated::kTypedComposition);

  aster::LocalChannel<1, 1, 40> channel;
  const aster::CoreRef core(aster::CoreHandles{
      .configurator = {},
      .logger = {},
      .executor = {},
      .channel = aster::ChannelRef(channel),
      .rpc = {},
      .parameter = {},
      .clock = {},
      .allocator = {},
      .hardware = {},
  });
  aster::generated::Composition composition(core);
  std::array<aster::RegistrySlot, 1> registries{{{&channel}}};
  aster::Runtime runtime(composition.Modules(), registries);

  if (!aster::IsOk(runtime.Initialize()) || !aster::IsOk(runtime.Start())) {
    return 1;
  }
  if (channel.stats().publications != 1 || channel.stats().deliveries != 1) {
    return 2;
  }
  runtime.Shutdown();
  return runtime.state() == aster::RuntimeState::kStopped ? 0 : 3;
}
