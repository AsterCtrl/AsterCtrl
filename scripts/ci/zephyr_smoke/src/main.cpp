/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq_offload.h>
#include <zephyr/sys/printk.h>

#include <array>
#include <atomic>
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

struct ExecutorProbe {
  std::atomic<std::uint32_t> callbacks{};
  std::atomic<std::uint32_t> callbacks_after_shutdown{};
  std::atomic<std::uint32_t> shutdowns{};
  std::atomic<bool> shutdown_started{};
};

void RecordExecutorCallback(void* state, const aster::ExecutionContext&) noexcept {
  auto& probe = *static_cast<ExecutorProbe*>(state);
  if (probe.shutdown_started.load(std::memory_order_acquire)) {
    probe.callbacks_after_shutdown.fetch_add(1, std::memory_order_relaxed);
  }
  probe.callbacks.fetch_add(1, std::memory_order_release);
}

class QueueingModule final : public aster::Module {
 public:
  explicit QueueingModule(ExecutorProbe& probe) noexcept : probe_(probe) {}

  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"queueing", "aster.smoke.QueueingModule", "zephyr-smoke", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    core_ = core;
    const aster::ExecutionContext caller{"initialize", aster::ExecutionKind::kThread, 0};
    return core_.executor().TryPost({RecordExecutorCallback, &probe_}, caller);
  }

  aster::Status Start() noexcept override {
    if (probe_.callbacks.load(std::memory_order_acquire) != 0) {
      return aster::Status::kInternal;
    }
    const aster::ExecutionContext caller{"start", aster::ExecutionKind::kThread, 0};
    return core_.executor().TryPost({RecordExecutorCallback, &probe_}, caller);
  }

  void Shutdown() noexcept override {
    probe_.shutdown_started.store(true, std::memory_order_release);
    probe_.shutdowns.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  ExecutorProbe& probe_;
  aster::CoreRef core_;
};

class GateObserverModule final : public aster::Module {
 public:
  GateObserverModule(ExecutorProbe& probe, aster::Status start_result,
                     bool test_isr_capacity) noexcept
      : probe_(probe), start_result_(start_result), test_isr_capacity_(test_isr_capacity) {}

  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"observer", "aster.smoke.GateObserverModule", "zephyr-smoke", {0, 2, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    core_ = core;
    return aster::Status::kOk;
  }

  aster::Status Start() noexcept override {
    if (probe_.callbacks.load(std::memory_order_acquire) != 0) {
      return aster::Status::kInternal;
    }
    if (test_isr_capacity_) {
      const aster::ExecutionContext interrupt{"isr", aster::ExecutionKind::kInterrupt, 0};
      capacity_status_ = core_.executor().TryPost({RecordExecutorCallback, &probe_}, interrupt);
      if (capacity_status_ != aster::Status::kCapacityExceeded) {
        return aster::Status::kInternal;
      }
    }
    return start_result_;
  }

  void Shutdown() noexcept override {
    probe_.shutdown_started.store(true, std::memory_order_release);
    probe_.shutdowns.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] aster::Status capacity_status() const noexcept { return capacity_status_; }

 private:
  ExecutorProbe& probe_;
  aster::CoreRef core_;
  aster::Status start_result_;
  aster::Status capacity_status_{aster::Status::kOk};
  bool test_isr_capacity_{};
};

template <aster::Status ObserverStart, bool TestIsrCapacity>
struct ExecutorLifecycleComposition {
  ExecutorProbe probe;
  QueueingModule queueing{probe};
  GateObserverModule observer{probe, ObserverStart, TestIsrCapacity};
  std::array<aster::ModuleSlot, 2> modules;
  std::array<aster::RegistrySlot, 0> registries{};

  explicit ExecutorLifecycleComposition(aster::CoreRef core) noexcept
      : modules{{{&queueing, core, "queueing"}, {&observer, core, "observer"}}} {}

  [[nodiscard]] std::span<aster::ModuleSlot> Modules() noexcept { return modules; }
  [[nodiscard]] std::span<aster::RegistrySlot> Registries() noexcept { return registries; }
};

using SuccessfulExecutorComposition = ExecutorLifecycleComposition<aster::Status::kOk, false>;
using FailingExecutorComposition = ExecutorLifecycleComposition<aster::Status::kUnavailable, true>;

K_THREAD_STACK_DEFINE(aster_success_executor_stack, 2048);
K_THREAD_STACK_DEFINE(aster_failure_executor_stack, 2048);

using SuccessfulExecutorRuntime =
    aster::platform::zephyr::StaticNodeRuntime<SuccessfulExecutorComposition, 2, 1, 1, 32, 1, 1,
                                               256, 1, 4>;
using FailingExecutorRuntime =
    aster::platform::zephyr::StaticNodeRuntime<FailingExecutorComposition, 2, 1, 1, 32, 1, 1, 256,
                                               1, 2>;

bool RunExecutorLifecycleSmoke() {
  static SuccessfulExecutorRuntime successful(aster_success_executor_stack,
                                              K_THREAD_STACK_SIZEOF(aster_success_executor_stack),
                                              "success", 0);
  if (successful.Initialize() != aster::Status::kOk ||
      successful.composition().probe.callbacks.load(std::memory_order_acquire) != 0 ||
      successful.Start() != aster::Status::kOk) {
    return false;
  }

  auto& success_probe = successful.composition().probe;
  for (std::size_t attempt = 0;
       attempt < 200 && success_probe.callbacks.load(std::memory_order_acquire) != 2; ++attempt) {
    k_sleep(K_MSEC(1));
  }
  if (success_probe.callbacks.load(std::memory_order_acquire) != 2) {
    return false;
  }

  const aster::ExecutionContext interrupt{"isr", aster::ExecutionKind::kInterrupt, 0};
  const auto deadline = successful.core().clock().NowNs() + 1'000'000'000ULL;
  if (successful.core().executor().TryPostAt(deadline, {RecordExecutorCallback, &success_probe},
                                             interrupt) != aster::Status::kOk) {
    return false;
  }
  successful.Shutdown();
  if (successful.state() != aster::RuntimeState::kStopped ||
      success_probe.callbacks.load(std::memory_order_acquire) != 2 ||
      success_probe.callbacks_after_shutdown.load(std::memory_order_acquire) != 0 ||
      success_probe.shutdowns.load(std::memory_order_acquire) != 2 ||
      successful.core().executor().TryPost({RecordExecutorCallback, &success_probe}, interrupt) !=
          aster::Status::kInvalidState) {
    return false;
  }

  static FailingExecutorRuntime failing(aster_failure_executor_stack,
                                        K_THREAD_STACK_SIZEOF(aster_failure_executor_stack),
                                        "failure", 0);
  if (failing.Initialize() != aster::Status::kOk ||
      failing.composition().probe.callbacks.load(std::memory_order_acquire) != 0 ||
      failing.Start() != aster::Status::kUnavailable ||
      failing.state() != aster::RuntimeState::kFailed ||
      failing.composition().observer.capacity_status() != aster::Status::kCapacityExceeded ||
      failing.composition().probe.callbacks.load(std::memory_order_acquire) != 0 ||
      failing.composition().probe.callbacks_after_shutdown.load(std::memory_order_acquire) != 0 ||
      failing.composition().probe.shutdowns.load(std::memory_order_acquire) != 2) {
    return false;
  }
  return true;
}

#if DT_HAS_CHOSEN(zephyr_canbus)
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_canbus), okay) && defined(CONFIG_CAN_LOOPBACK)
#define ASTER_CAN_ADAPTER_SMOKE 1
#endif
#endif

#if defined(ASTER_CAN_ADAPTER_SMOKE)

struct CanCapture {
  static constexpr std::size_t kCapacity = 8;

  std::array<aster::transport::can::CanFrame, kCapacity> frames{};
  std::array<std::uint64_t, kCapacity> receive_times_ns{};
  aster::platform::zephyr::CanDeviceAdapter* adapter{};
  aster::Status stop_status{aster::Status::kUnavailable};
  std::size_t count{};
  bool stop_when_full{};
};

aster::Status CaptureCanFrame(void* state, const aster::transport::can::CanFrame& frame,
                              std::uint64_t receive_time_ns,
                              const aster::ExecutionContext& caller) noexcept {
  if (state == nullptr || caller.kind() != aster::ExecutionKind::kThread) {
    return aster::Status::kInvalidArgument;
  }
  auto& capture = *static_cast<CanCapture*>(state);
  if (capture.count == capture.frames.size()) {
    return aster::Status::kCapacityExceeded;
  }
  capture.frames[capture.count] = frame;
  capture.receive_times_ns[capture.count] = receive_time_ns;
  ++capture.count;
  if (capture.stop_when_full && capture.count == capture.frames.size()) {
    capture.stop_status = capture.adapter->Stop();
  }
  return aster::Status::kOk;
}

struct CanIsrProbe {
  aster::platform::zephyr::CanDeviceAdapter* adapter{};
  const aster::transport::can::CanFrame* frame{};
  aster::Status send_status{aster::Status::kOk};
  aster::Status poll_status{aster::Status::kOk};
};

void ProbeCanFromIsr(const void* state) {
  auto& probe = *const_cast<CanIsrProbe*>(static_cast<const CanIsrProbe*>(state));
  const aster::ExecutionContext thread("isr-probe", aster::ExecutionKind::kThread, 0);
  probe.send_status = probe.adapter->Send(*probe.frame, thread);
  probe.poll_status = probe.adapter->Poll(thread);
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

  static aster::platform::zephyr::CanCallbackFence adapter_callbacks;
  aster::platform::zephyr::CanDeviceAdapter adapter(*can_device, adapter_callbacks);
  aster::platform::zephyr::CanDeviceAdapter duplicate_adapter(*can_device, adapter_callbacks);
  if (duplicate_adapter.Ready() != aster::Status::kInvalidState) {
    return false;
  }
  CanCapture capture;
  capture.adapter = &adapter;
  capture.stop_when_full = true;
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
    static_cast<void>(adapter.Stop());
    return false;
  }

  const aster::ExecutionContext thread("can", aster::ExecutionKind::kThread, 0);
  CanIsrProbe isr_probe{&adapter, &frame};
  irq_offload(ProbeCanFromIsr, &isr_probe);
  if (isr_probe.send_status != aster::Status::kInvalidArgument ||
      isr_probe.poll_status != aster::Status::kInvalidArgument) {
    static_cast<void>(adapter.Stop());
    return false;
  }

  for (std::size_t sequence = 0; sequence < CanCapture::kCapacity; ++sequence) {
    frame.data[0] = static_cast<std::byte>(sequence);
    if (adapter.writer().Send(frame, thread) != aster::Status::kOk) {
      static_cast<void>(adapter.Stop());
      return false;
    }
  }
  for (std::size_t attempt = 0; attempt < 1000 && capture.count < CanCapture::kCapacity;
       ++attempt) {
    static_cast<void>(adapter.Poll(thread));
    k_sleep(K_MSEC(1));
  }

  const auto stats = adapter.stats();
  bool valid =
      capture.count == CanCapture::kCapacity && capture.stop_status == aster::Status::kOk &&
      stats.tx_frames == CanCapture::kCapacity && stats.tx_completions == CanCapture::kCapacity &&
      stats.rx_frames == CanCapture::kCapacity && stats.rx_dropped == 0 &&
      stats.invalid_frames == 0;
  for (std::size_t sequence = 0; sequence < capture.count; ++sequence) {
    valid = valid && capture.frames[sequence].arbitration_id == frame.arbitration_id &&
            capture.frames[sequence].size == frame.size &&
            capture.frames[sequence].data[0] == static_cast<std::byte>(sequence);
  }
  if (!valid || adapter.Stop() != aster::Status::kOk || adapter.running() ||
      adapter.state() != aster::platform::zephyr::CanDeviceState::kStopped ||
      adapter.Send(frame, thread) != aster::Status::kInvalidState) {
    return false;
  }

  static aster::platform::zephyr::CanCallbackFence stopping_callbacks;
  aster::platform::zephyr::CanDeviceAdapter stopping_adapter(*can_device, stopping_callbacks);
  CanCapture stopping_capture;
  if (stopping_adapter.Start({CaptureCanFrame, &stopping_capture}, {0x321, CAN_STD_ID_MASK}) !=
      aster::Status::kOk) {
    return false;
  }
  for (std::size_t sequence = 0; sequence < CanCapture::kCapacity; ++sequence) {
    frame.data[0] = static_cast<std::byte>(sequence);
    if (stopping_adapter.Send(frame, thread) != aster::Status::kOk) {
      static_cast<void>(stopping_adapter.Stop());
      return false;
    }
  }
  if (stopping_adapter.Stop() != aster::Status::kOk) {
    return false;
  }
  const auto stopping_stats = stopping_adapter.stats();
  return stopping_adapter.state() == aster::platform::zephyr::CanDeviceState::kStopped &&
         stopping_stats.tx_frames == CanCapture::kCapacity &&
         stopping_stats.tx_completions + stopping_stats.tx_failures + stopping_stats.tx_aborted ==
             stopping_stats.tx_frames;
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
  if (!RunExecutorLifecycleSmoke()) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL executor lifecycle\n");
    return 1;
  }
  if (!RunCanAdapterSmoke()) {
    printk("ASTERCTRL_RUNTIME_SMOKE: FAIL CAN Adapter\n");
    return 1;
  }
  printk("ASTERCTRL_RUNTIME_SMOKE: PASS\n");
  return 0;
}
