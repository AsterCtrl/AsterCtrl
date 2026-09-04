/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>

#include "composition.generated.hpp"

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "aster/platform/zephyr/runtime_services.hpp"
#else
#include <iostream>
#endif

#if defined(__ZEPHYR__)
namespace {

K_THREAD_STACK_DEFINE(aster_generated_executor_stack, CONFIG_ASTERCTRL_STACK_BYTES);

using NodeRuntime =
    aster::platform::zephyr::ConfiguredStaticNodeRuntime<aster::generated::Composition>;

constexpr std::string_view ExecutorName() noexcept {
  return aster::generated::kExecutors.empty() ? std::string_view{"aster"}
                                              : aster::generated::kExecutors.front().domain;
}

constexpr int ExecutorPriority() noexcept {
  return aster::generated::kExecutors.empty() ? 0 : aster::generated::kExecutors.front().priority;
}

int Fail(std::string_view phase, aster::Status status) noexcept {
  printk("ASTERCTRL_GENERATED_NODE: FAIL %.*s status=0x%08x\n", static_cast<int>(phase.size()),
         phase.data(), static_cast<unsigned int>(status));
  return 1;
}

}  // namespace
#endif

int main() {
#if defined(__ZEPHYR__)
  static NodeRuntime node(aster_generated_executor_stack,
                          K_THREAD_STACK_SIZEOF(aster_generated_executor_stack), ExecutorName(),
                          ExecutorPriority());
  auto& composition = node.composition();
#else
  aster::generated::Composition composition;
#endif
  if (!aster::generated::kTypedComposition || composition.Modules().empty()) {
    return 1;
  }
  for (const auto& slot : composition.Modules()) {
    if (slot.module == nullptr || slot.instance_name.empty() || slot.module->Info().type.empty()) {
      return 2;
    }
  }
#if defined(__ZEPHYR__)
  auto status = aster::generated::RegisterHardwareBindings(node);
  if (!aster::IsOk(status)) {
    return Fail("hardware", status);
  }
  if (aster::generated::kRequiresTransportWiring) {
    printk(
        "ASTERCTRL_GENERATED_NODE: BLOCKED transport wiring required routes=%u "
        "status=0x%08x\n",
        static_cast<unsigned int>(aster::generated::kExternalRouteCount),
        static_cast<unsigned int>(aster::Status::kUnavailable));
    return 2;
  }
  status = aster::generated::RegisterGeneratedTransports(node);
  if (!aster::IsOk(status)) {
    return Fail("transport", status);
  }
  status = node.Initialize();
  if (!aster::IsOk(status)) {
    return Fail("initialize", status);
  }
  status = node.Start();
  if (!aster::IsOk(status)) {
    return Fail("start", status);
  }
  printk("ASTERCTRL_GENERATED_NODE: RUNNING %.*s\n",
         static_cast<int>(aster::generated::kNodes[0].name.size()),
         aster::generated::kNodes[0].name.data());
  k_sleep(K_FOREVER);
#else
  std::cout << "ASTERCTRL_GENERATED_NODE: PASS " << aster::generated::kNodes[0].name << '\n';
#endif
  return 0;
}
