#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "aster/status.hpp"
#include "aster/transport/can/link.hpp"

namespace aster::platform::zephyr {

struct CanReceiveFilter {
  std::uint16_t arbitration_id{};
  std::uint16_t mask{};
};

struct CanDeviceStats {
  std::uint32_t tx_frames{};
  std::uint32_t tx_completions{};
  std::uint32_t tx_failures{};
  std::uint32_t rx_frames{};
  std::uint32_t rx_dropped{};
  std::uint32_t invalid_frames{};
  std::uint32_t receiver_failures{};
};

class CanDeviceAdapter {
 public:
  explicit CanDeviceAdapter(const device& can_device) noexcept;

  CanDeviceAdapter(const CanDeviceAdapter&) = delete;
  CanDeviceAdapter& operator=(const CanDeviceAdapter&) = delete;
  CanDeviceAdapter(CanDeviceAdapter&&) = delete;
  CanDeviceAdapter& operator=(CanDeviceAdapter&&) = delete;

  [[nodiscard]] Status Ready() const noexcept;
  Status Start(transport::can::CanFrameReceiver receiver, CanReceiveFilter filter = {}) noexcept;
  Status Send(const transport::can::CanFrame& frame, const ExecutionContext& caller) noexcept;
  Status Poll(const ExecutionContext& caller) noexcept;
  void Stop() noexcept;

  [[nodiscard]] transport::can::CanFrameWriter writer() noexcept;
  [[nodiscard]] CanDeviceStats stats() const noexcept;
  [[nodiscard]] bool running() const noexcept { return running_; }

 private:
#if defined(CONFIG_ASTERCTRL_CAN_RX_QUEUE_DEPTH)
  static constexpr std::size_t kReceiveQueueDepth = CONFIG_ASTERCTRL_CAN_RX_QUEUE_DEPTH;
#else
  static constexpr std::size_t kReceiveQueueDepth = 16;
#endif
  static_assert(kReceiveQueueDepth > 0);

  struct QueuedFrame {
    can_frame frame{};
    std::uint64_t receive_time_ns{};
  };

  static Status Write(void* state, const transport::can::CanFrame& frame,
                      const ExecutionContext& caller) noexcept;
  static void Receive(const device* can_device, can_frame* frame, void* state) noexcept;
  static void TransmitComplete(const device* can_device, int error, void* state) noexcept;

  const device& can_device_;
  transport::can::CanFrameReceiver receiver_{};
  k_msgq receive_queue_{};
  alignas(4) std::array<char, kReceiveQueueDepth * sizeof(QueuedFrame)> receive_storage_{};
  int filter_id_{-1};
  bool running_{};
  atomic_t tx_frames_{};
  atomic_t tx_completions_{};
  atomic_t tx_failures_{};
  atomic_t rx_frames_{};
  atomic_t rx_dropped_{};
  atomic_t invalid_frames_{};
  atomic_t receiver_failures_{};
};

}  // namespace aster::platform::zephyr
