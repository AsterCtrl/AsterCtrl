#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "spsc_queue.hpp"
#include "xrobot/backend/libxr/classic_can_endpoint.hpp"
#include "xrobot/transport/can/link.hpp"

namespace xrobot::backend::libxr {

struct CanAdapterStats {
  std::uint32_t tx_frames{};
  std::uint32_t tx_failures{};
  std::uint32_t rx_frames{};
  std::uint32_t rx_invalid{};
  std::uint32_t rx_dropped{};
  std::uint32_t rx_queue_high_water{};
  std::uint32_t dispatched{};
  std::uint32_t dispatch_failures{};
};

struct CanFilterRange {
  std::uint16_t first_id{};
  std::uint16_t last_id{};
};

template <std::size_t ReceiveQueueCapacity>
class CanAdapter {
 public:
  static_assert(ReceiveQueueCapacity > 0);
  static_assert(ReceiveQueueCapacity <= UINT32_MAX);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "CAN ISR counters require lock-free 32-bit atomics");

  explicit CanAdapter(ClassicCanEndpoint& endpoint)
      : endpoint_(endpoint), receive_queue_(ReceiveQueueCapacity) {}

  CanAdapter(ClassicCanEndpoint& endpoint,
             std::span<const CanFilterRange> filters) noexcept
      : endpoint_(endpoint),
        filters_(filters),
        receive_queue_(ReceiveQueueCapacity) {}

  CanAdapter(const CanAdapter&) = delete;
  CanAdapter& operator=(const CanAdapter&) = delete;
  CanAdapter(CanAdapter&&) = delete;
  CanAdapter& operator=(CanAdapter&&) = delete;

  xrobot::runtime::Status BindReceiver(
      xrobot::transport::can::CanFrameReceiver receiver) noexcept {
    using xrobot::runtime::Status;

    if (initialized_ || receiver_bound_) {
      return Status::kInvalidState;
    }
    if (receiver.receive == nullptr) {
      return Status::kInvalidArgument;
    }
    receiver_ = receiver;
    receiver_bound_ = true;
    return Status::kOk;
  }

  xrobot::runtime::Status Initialize() {
    if (initialized_) {
      return xrobot::runtime::Status::kInvalidState;
    }
    if (!receiver_bound_) {
      return xrobot::runtime::Status::kInvalidState;
    }
    if (filters_.empty()) {
      const auto status = endpoint_.Subscribe(1U, 0x7ffU,
                                              {ReceiveThunk, this});
      if (status != xrobot::runtime::Status::kOk) return status;
    } else {
      std::uint16_t previous_last{};
      for (const auto& filter : filters_) {
        if (filter.first_id == 0U || filter.first_id > filter.last_id ||
            filter.last_id > 0x7ffU || filter.first_id <= previous_last) {
          return xrobot::runtime::Status::kInvalidArgument;
        }
        previous_last = filter.last_id;
      }
      for (const auto& filter : filters_) {
        const auto status = endpoint_.Subscribe(
            filter.first_id, filter.last_id, {ReceiveThunk, this});
        if (status != xrobot::runtime::Status::kOk) return status;
      }
    }
    initialized_ = true;
    return xrobot::runtime::Status::kOk;
  }

  xrobot::transport::can::CanFrameWriter writer() noexcept {
    return {WriteThunk, this};
  }

  xrobot::runtime::Status Drain(
      const xrobot::runtime::ExecutionContext& context,
      std::size_t maximum_frames = ReceiveQueueCapacity) noexcept {
    using xrobot::runtime::ExecutionKind;
    using xrobot::runtime::Status;

    if (!initialized_) {
      return Status::kInvalidState;
    }
    if (context.kind() != ExecutionKind::kThread || maximum_frames == 0) {
      return Status::kInvalidArgument;
    }

    QueuedFrame queued;
    std::size_t processed{};
    auto result = Status::kOk;
    while (processed < maximum_frames &&
           receive_queue_.Pop(queued) == LibXR::ErrorCode::OK) {
      const auto status = receiver_.Accept(queued.frame, queued.receive_time_ns,
                                           context);
      if (status == Status::kOk) {
        dispatched_.fetch_add(1, std::memory_order_relaxed);
      } else {
        dispatch_failures_.fetch_add(1, std::memory_order_relaxed);
        if (result == Status::kOk &&
            (status == Status::kInvalidState || status == Status::kInternal)) {
          result = status;
        }
      }
      ++processed;
    }
    return processed == 0 ? Status::kUnavailable : result;
  }

  CanAdapterStats stats() const noexcept {
    return {
        tx_frames_.load(std::memory_order_relaxed),
        tx_failures_.load(std::memory_order_relaxed),
        rx_frames_.load(std::memory_order_relaxed),
        rx_invalid_.load(std::memory_order_relaxed),
        rx_dropped_.load(std::memory_order_relaxed),
        rx_queue_high_water_.load(std::memory_order_relaxed),
        dispatched_.load(std::memory_order_relaxed),
        dispatch_failures_.load(std::memory_order_relaxed),
    };
  }

 private:
  struct QueuedFrame {
    xrobot::transport::can::CanFrame frame;
    std::uint64_t receive_time_ns{};
  };

  static xrobot::runtime::Status WriteThunk(
      void* state, const xrobot::transport::can::CanFrame& frame,
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    return static_cast<CanAdapter*>(state)->Write(frame, caller);
  }

  xrobot::runtime::Status Write(
      const xrobot::transport::can::CanFrame& frame,
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    using xrobot::runtime::Status;
    using xrobot::transport::can::CanArbitrationId;

    if (!initialized_) {
      return Status::kInvalidState;
    }
    if (!CanArbitrationId::Decode(frame.arbitration_id).has_value() ||
        frame.size == 0 || frame.size > frame.data.size()) {
      return Status::kInvalidArgument;
    }

    ClassicCanFrame pack{};
    pack.id = frame.arbitration_id;
    pack.size = frame.size;
    for (std::size_t index = 0; index < frame.size; ++index) {
      pack.data[index] = frame.data[index];
    }
    const auto status = endpoint_.Write(pack, caller);
    if (status == Status::kOk) {
      tx_frames_.fetch_add(1, std::memory_order_relaxed);
    } else {
      tx_failures_.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
  }

  static void ReceiveThunk(void* state, const ClassicCanFrame& frame,
                           std::uint64_t receive_time_ns,
                           bool) noexcept {
    static_cast<CanAdapter*>(state)->Receive(frame, receive_time_ns);
  }

  void Receive(const ClassicCanFrame& pack,
               std::uint64_t receive_time_ns) noexcept {
    using xrobot::transport::can::CanArbitrationId;

    if (pack.id > 0x7ffU || pack.size == 0 || pack.size > 8 ||
        !CanArbitrationId::Decode(static_cast<std::uint16_t>(pack.id))
             .has_value()) {
      rx_invalid_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    QueuedFrame queued;
    queued.frame.arbitration_id = static_cast<std::uint16_t>(pack.id);
    queued.frame.size = pack.size;
    for (std::size_t index = 0; index < pack.size; ++index) {
      queued.frame.data[index] = pack.data[index];
    }
    queued.receive_time_ns = receive_time_ns;
    if (receive_queue_.Push(queued) != LibXR::ErrorCode::OK) {
      rx_dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const auto depth = static_cast<std::uint32_t>(receive_queue_.Size());
    auto high_water = rx_queue_high_water_.load(std::memory_order_relaxed);
    while (depth > high_water &&
           !rx_queue_high_water_.compare_exchange_weak(
               high_water, depth, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    rx_frames_.fetch_add(1, std::memory_order_relaxed);
  }

  ClassicCanEndpoint& endpoint_;
  std::span<const CanFilterRange> filters_;
  xrobot::transport::can::CanFrameReceiver receiver_;
  LibXR::SPSCQueue<QueuedFrame> receive_queue_;
  bool receiver_bound_{};
  bool initialized_{};
  std::atomic<std::uint32_t> tx_frames_{};
  std::atomic<std::uint32_t> tx_failures_{};
  std::atomic<std::uint32_t> rx_frames_{};
  std::atomic<std::uint32_t> rx_invalid_{};
  std::atomic<std::uint32_t> rx_dropped_{};
  std::atomic<std::uint32_t> rx_queue_high_water_{};
  std::atomic<std::uint32_t> dispatched_{};
  std::atomic<std::uint32_t> dispatch_failures_{};
};

}  // namespace xrobot::backend::libxr
