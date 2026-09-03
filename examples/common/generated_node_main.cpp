/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>

#include "composition.generated.hpp"

#if defined(__ZEPHYR__)
#include <zephyr/sys/printk.h>
#else
#include <iostream>
#endif

int main() {
  aster::generated::Composition composition;
  if (!aster::generated::kTypedComposition || composition.Modules().empty()) {
    return 1;
  }
  for (const auto& slot : composition.Modules()) {
    if (slot.module == nullptr || slot.instance_name.empty() || slot.module->Info().type.empty()) {
      return 2;
    }
  }
#if defined(__ZEPHYR__)
  printk("ASTERCTRL_GENERATED_NODE: PASS %s\n", aster::generated::kNodes[0].name.data());
#else
  std::cout << "ASTERCTRL_GENERATED_NODE: PASS " << aster::generated::kNodes[0].name << '\n';
#endif
  return 0;
}
