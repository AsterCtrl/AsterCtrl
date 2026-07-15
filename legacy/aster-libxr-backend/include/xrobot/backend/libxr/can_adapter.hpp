#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "can.hpp"
#include "spsc_queue.hpp"
#include "xrobot/transport/can/link.hpp"

namespace xrobot::backend::libxr {

struct CanAdapterStats {
  std::uint32_t tx_frames{};
  std::uint32_t tx_failures{};
  std::uint32_t rx_frames{};
  std::uint32_t rx_invalid{};
  std::uint32_t rx_dropped{};
  std::uint32_t dispatched{};
  std::uint32_t dispatch_failures{};
};

template <std::size_t ReceiveQueueCapacity>
class CanAdapter {
 public:
  static_assert(ReceiveQueueCapacity > 0);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "CAN ISR counters require lock-free 32-bit atomics");

  CanAdapter(LibXR::CAN& can,
             xrobot::transport::can::CanFrameReceiver receiver,
             xrobot::transport::can::CanClockReader clock)
      : can_(can),
        receiver_(receiver),
        clock_(clock),
        receive_queue_(ReceiveQueueCapacity) {}

  CanAdapter(const CanAdapter&) = delete;
  CanAdapter& operator=(const CanAdapter&) = delete;
  CanAdapter(CanAdapter&&) = delete;
  CanAdapter& operator=(CanAdapter&&) = delete;

  xrobot::runtime::Status Initialize() {
    if (initialized_) {
      return xrobot::runtime::Status::kInvalidState;
    }
    if (receiver_.receive == nullptr || clock_.read == nullptr) {
      return xrobot::runtime::Status::kInvalidArgument;
    }
    receive_callback_ = LibXR::CAN::Callback::Create(ReceiveThunk, this);
    can_.Register(receive_callback_, LibXR::CAN::Type::STANDARD,
                  LibXR::CAN::FilterMode::ID_RANGE, 1, 0x7ffU);
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
        if (result == Status::kOk) {
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
      const xrobot::runtime::ExecutionContext&) noexcept {
    return static_cast<CanAdapter*>(state)->Write(frame);
  }

  xrobot::runtime::Status Write(
      const xrobot::transport::can::CanFrame& frame) noexcept {
    using xrobot::runtime::Status;
    using xrobot::transport::can::CanArbitrationId;

    if (!initialized_) {
      return Status::kInvalidState;
    }
    if (!CanArbitrationId::Decode(frame.arbitration_id).has_value() ||
        frame.size == 0 || frame.size > frame.data.size()) {
      return Status::kInvalidArgument;
    }

    LibXR::CAN::ClassicPack pack{};
    pack.id = frame.arbitration_id;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = frame.size;
    for (std::size_t index = 0; index < frame.size; ++index) {
      pack.data[index] = std::to_integer<std::uint8_t>(frame.data[index]);
    }
    const auto status = MapError(can_.AddMessage(pack));
    if (status == Status::kOk) {
      tx_frames_.fetch_add(1, std::memory_order_relaxed);
    } else {
      tx_failures_.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
  }

  static void ReceiveThunk(bool, CanAdapter* self,
                           const LibXR::CAN::ClassicPack& pack) {
    self->Receive(pack);
  }

  void Receive(const LibXR::CAN::ClassicPack& pack) noexcept {
    using xrobot::transport::can::CanArbitrationId;

    if (pack.type != LibXR::CAN::Type::STANDARD || pack.id > 0x7ffU ||
        pack.dlc == 0 || pack.dlc > 8 ||
        !CanArbitrationId::Decode(static_cast<std::uint16_t>(pack.id))
             .has_value()) {
      rx_invalid_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    QueuedFrame queued;
    queued.frame.arbitration_id = static_cast<std::uint16_t>(pack.id);
    queued.frame.size = pack.dlc;
    for (std::size_t index = 0; index < pack.dlc; ++index) {
      queued.frame.data[index] = static_cast<std::byte>(pack.data[index]);
    }
    queued.receive_time_ns = clock_.NowNs();
    if (receive_queue_.Push(queued) != LibXR::ErrorCode::OK) {
      rx_dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    rx_frames_.fetch_add(1, std::memory_order_relaxed);
  }

  static constexpr xrobot::runtime::Status MapError(
      LibXR::ErrorCode error) noexcept {
    using xrobot::runtime::Status;

    switch (error) {
      case LibXR::ErrorCode::OK:
      case LibXR::ErrorCode::PENDING:
        return Status::kOk;
      case LibXR::ErrorCode::ARG_ERR:
      case LibXR::ErrorCode::SIZE_ERR:
      case LibXR::ErrorCode::PTR_NULL:
      case LibXR::ErrorCode::OUT_OF_RANGE:
        return Status::kInvalidArgument;
      case LibXR::ErrorCode::INIT_ERR:
      case LibXR::ErrorCode::STATE_ERR:
        return Status::kInvalidState;
      case LibXR::ErrorCode::NO_MEM:
      case LibXR::ErrorCode::NO_BUFF:
      case LibXR::ErrorCode::FULL:
        return Status::kCapacityExceeded;
      case LibXR::ErrorCode::NO_RESPONSE:
      case LibXR::ErrorCode::TIMEOUT:
        return Status::kTimeout;
      case LibXR::ErrorCode::NOT_FOUND:
      case LibXR::ErrorCode::EMPTY:
      case LibXR::ErrorCode::BUSY:
        return Status::kUnavailable;
      case LibXR::ErrorCode::FAILED:
      case LibXR::ErrorCode::CHECK_ERR:
      case LibXR::ErrorCode::NOT_SUPPORT:
        return Status::kInternal;
    }
    return Status::kInternal;
  }

  LibXR::CAN& can_;
  xrobot::transport::can::CanFrameReceiver receiver_;
  xrobot::transport::can::CanClockReader clock_;
  LibXR::SPSCQueue<QueuedFrame> receive_queue_;
  LibXR::CAN::Callback receive_callback_;
  bool initialized_{};
  std::atomic<std::uint32_t> tx_frames_{};
  std::atomic<std::uint32_t> tx_failures_{};
  std::atomic<std::uint32_t> rx_frames_{};
  std::atomic<std::uint32_t> rx_invalid_{};
  std::atomic<std::uint32_t> rx_dropped_{};
  std::atomic<std::uint32_t> dispatched_{};
  std::atomic<std::uint32_t> dispatch_failures_{};
};

}  // namespace xrobot::backend::libxr
