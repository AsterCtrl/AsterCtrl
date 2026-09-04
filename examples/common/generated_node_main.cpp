/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>
#include <string_view>

#include "composition.generated.hpp"

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "aster/platform/zephyr/runtime_services.hpp"
#else
#include <chrono>
#include <iostream>
#include <thread>

#include "aster/platform/linux/node_runtime.hpp"
#include "aster/platform/linux/shutdown_signal.hpp"
#endif

namespace {

constexpr std::string_view ExecutorName() noexcept {
  return aster::generated::kExecutors.empty() ? std::string_view{"aster"}
                                              : aster::generated::kExecutors.front().domain;
}

bool ValidComposition(aster::generated::Composition& composition) noexcept {
  if (!aster::generated::kTypedComposition || composition.Modules().empty()) {
    return false;
  }
  for (const auto& slot : composition.Modules()) {
    if (slot.module == nullptr || slot.instance_name.empty() || slot.module->Info().type.empty()) {
      return false;
    }
  }
  return true;
}

#if defined(__ZEPHYR__)
K_THREAD_STACK_DEFINE(aster_generated_executor_stack, CONFIG_ASTERCTRL_STACK_BYTES);

using NodeRuntime =
    aster::platform::zephyr::ConfiguredStaticNodeRuntime<aster::generated::Composition>;

constexpr int ExecutorPriority() noexcept {
  return aster::generated::kExecutors.empty() ? 0 : aster::generated::kExecutors.front().priority;
}

int Fail(std::string_view phase, aster::Status status) noexcept {
  printk("ASTERCTRL_GENERATED_NODE: FAIL %.*s status=0x%08x\n", static_cast<int>(phase.size()),
         phase.data(), static_cast<unsigned int>(status));
  return 1;
}
#else
using NodeRuntime = aster::platform::linux::StaticNodeRuntime<
    aster::generated::Composition, aster::generated::kRuntimeChannelCapacity,
    aster::generated::kRuntimeSubscriberCapacity, aster::generated::kRuntimeMaximumMessageSize,
    aster::generated::kRuntimeRpcCapacity, aster::generated::kRuntimeRpcCapacity,
    aster::generated::kRuntimeHardwareCapacity, aster::generated::kRuntimeExecutorQueueDepth>;

int Fail(std::string_view phase, aster::Status status) noexcept {
  std::cerr << "ASTERCTRL_GENERATED_NODE: FAIL " << phase << " status=0x" << std::hex
            << static_cast<unsigned int>(status) << '\n';
  return 1;
}
#endif

}  // namespace

#if defined(__ZEPHYR__)
int main() {
  static NodeRuntime node(aster_generated_executor_stack,
                          K_THREAD_STACK_SIZEOF(aster_generated_executor_stack), ExecutorName(),
                          ExecutorPriority());
  auto& composition = node.composition();
  if (!ValidComposition(composition)) {
    return 1;
  }
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
  return 0;
}
#else
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--check") {
    aster::generated::Composition composition;
    if (!ValidComposition(composition)) {
      return 1;
    }
    std::cout << "ASTERCTRL_GENERATED_NODE: CHECK " << aster::generated::kNodes[0].name << '\n';
    return 0;
  }
  if (argc != 1) {
    std::cerr << "usage: aster_generated_node [--check]\n";
    return 64;
  }

  aster::platform::linux::ShutdownSignal shutdown;
  auto status = shutdown.Install();
  if (!aster::IsOk(status)) {
    return Fail("signal", status);
  }

  NodeRuntime node(aster::generated::kDeploymentIdentity, ExecutorName());
  if (!ValidComposition(node.composition())) {
    return 1;
  }
  status = aster::generated::RegisterHardwareBindings(node);
  if (!aster::IsOk(status)) {
    return Fail("hardware", status);
  }
  if (aster::generated::kRequiresTransportWiring) {
    std::cerr << "ASTERCTRL_GENERATED_NODE: BLOCKED transport wiring required routes="
              << aster::generated::kExternalRouteCount << '\n';
    return 2;
  }
  status = aster::generated::RegisterGeneratedTransports(node);
  if (!aster::IsOk(status)) {
    return Fail("transport", status);
  }
  status = node.Start();
  if (!aster::IsOk(status)) {
    return Fail("start", status);
  }
  std::cout << "ASTERCTRL_GENERATED_NODE: RUNNING " << aster::generated::kNodes[0].name << '\n'
            << std::flush;
  while (!shutdown.requested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  node.Shutdown();
  return node.state() == aster::platform::linux::SupervisorState::kStopped ? 0 : 3;
}
#endif
