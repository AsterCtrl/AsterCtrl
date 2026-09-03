/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>

#include "examples/common/portable_pubsub.hpp"

int main() {
  aster::examples::PortablePubSubComposition composition;
  if (!aster::IsOk(composition.Run())) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL pubsub lifecycle\n");
    return 1;
  }
  if (!composition.sink().received() || composition.sink().sequence() != 42) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL pubsub contract\n");
    return 1;
  }
  composition.Shutdown();
  if (composition.state() != aster::RuntimeState::kStopped) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL shutdown\n");
    return 1;
  }
  printk("ASTERCTRL_RUNTIME_SMOKE: PASS\n");
  return 0;
}
