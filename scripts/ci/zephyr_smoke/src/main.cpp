/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/platform/zephyr/can_device.hpp"
#include "aster/platform/zephyr/runtime_services.hpp"
#include "examples/common/portable_pubsub.hpp"

namespace {

struct SmokeComposition {
  aster::examples::PulseSink sink;
  aster::examples::PulseSource source;
  std::array<aster::ModuleSlot, 2> modules;
  std::array<aster::RegistrySlot, 0> registries{};

  explicit SmokeComposition(aster::CoreRef core) noexcept
      : modules{{{&sink, core, "sink"}, {&source, core, "source"}}} {}

  [[nodiscard]] std::span<aster::ModuleSlot> Modules() noexcept { return modules; }
  [[nodiscard]] std::span<aster::RegistrySlot> Registries() noexcept { return registries; }
};

K_THREAD_STACK_DEFINE(aster_smoke_executor_stack, 2048);

using SmokeRuntime =
    aster::platform::zephyr::StaticNodeRuntime<SmokeComposition, 2, 1, 1, 32, 1, 1, 256, 1, 2>;

#if DT_HAS_CHOSEN(zephyr_canbus)
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_canbus), okay) && defined(CONFIG_CAN_LOOPBACK)
#define ASTER_CAN_ADAPTER_SMOKE 1
#endif
#endif

#if defined(ASTER_CAN_ADAPTER_SMOKE)

struct CanCapture {
  aster::transport::can::CanFrame frame{};
  std::uint64_t receive_time_ns{};
  bool received{};
};

aster::Status CaptureCanFrame(void* state, const aster::transport::can::CanFrame& frame,
                              std::uint64_t receive_time_ns,
                              const aster::ExecutionContext& caller) noexcept {
  if (state == nullptr || caller.kind() != aster::ExecutionKind::kThread) {
    return aster::Status::kInvalidArgument;
  }
  auto& capture = *static_cast<CanCapture*>(state);
  capture.frame = frame;
  capture.receive_time_ns = receive_time_ns;
  capture.received = true;
  return aster::Status::kOk;
}

bool RunCanAdapterSmoke() {
  const auto* can_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
  if (!device_is_ready(can_device)) {
    return false;
  }
  static_cast<void>(can_stop(can_device));
  if (can_set_mode(can_device, CAN_MODE_LOOPBACK) != 0) {
    return false;
  }

  aster::platform::zephyr::CanDeviceAdapter adapter(*can_device);
  CanCapture capture;
  if (adapter.Start({CaptureCanFrame, &capture}, {0x321, CAN_STD_ID_MASK}) != aster::Status::kOk) {
    return false;
  }

  aster::transport::can::CanFrame frame;
  frame.arbitration_id = 0x321;
  frame.size = 3;
  frame.data[0] = std::byte{0x11};
  frame.data[1] = std::byte{0x22};
  frame.data[2] = std::byte{0x33};
  const aster::ExecutionContext interrupt("can-rx", aster::ExecutionKind::kInterrupt, 0);
  if (adapter.Send(frame, interrupt) != aster::Status::kInvalidArgument) {
    adapter.Stop();
    return false;
  }

  const aster::ExecutionContext thread("can", aster::ExecutionKind::kThread, 0);
  if (adapter.writer().Send(frame, thread) != aster::Status::kOk) {
    adapter.Stop();
    return false;
  }
  for (std::size_t attempt = 0; attempt < 200 && !capture.received; ++attempt) {
    static_cast<void>(adapter.Poll(thread));
    k_sleep(K_MSEC(1));
  }

  const auto stats = adapter.stats();
  const bool valid = capture.received && capture.frame.arbitration_id == frame.arbitration_id &&
                     capture.frame.size == frame.size && capture.frame.data == frame.data &&
                     stats.tx_frames == 1 && stats.tx_completions == 1 && stats.rx_frames == 1 &&
                     stats.rx_dropped == 0 && stats.invalid_frames == 0;
  adapter.Stop();
  return valid && !adapter.running() && adapter.Send(frame, thread) == aster::Status::kInvalidState;
}

#else

bool RunCanAdapterSmoke() { return true; }

#endif

#undef ASTER_CAN_ADAPTER_SMOKE

}  // namespace

int main() {
  static SmokeRuntime runtime(aster_smoke_executor_stack,
                              K_THREAD_STACK_SIZEOF(aster_smoke_executor_stack), "smoke", 0);
  if (!aster::IsOk(runtime.Initialize()) || runtime.state() != aster::RuntimeState::kInitialized) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL initialize and seal\n");
    return 1;
  }
  if (!aster::IsOk(runtime.Start()) || runtime.state() != aster::RuntimeState::kRunning) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL start\n");
    return 1;
  }
  const auto& sink = runtime.composition().sink;
  if (!sink.received() || sink.sequence() != 42) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL pubsub contract\n");
    return 1;
  }
  runtime.Shutdown();
  if (runtime.state() != aster::RuntimeState::kStopped) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL shutdown\n");
    return 1;
  }
  if (!RunCanAdapterSmoke()) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL CAN Adapter\n");
    return 1;
  }
  printk("ASTERCTRL_RUNTIME_SMOKE: PASS\n");
  return 0;
}
