#include "aster/platform/zephyr/can_device.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace aster::platform::zephyr {
namespace {

Status FromZephyrError(int error) noexcept {
  switch (error) {
    case 0:
      return Status::kOk;
    case -EINVAL:
      return Status::kInvalidArgument;
    case -ENOSPC:
    case -EAGAIN:
    case -EBUSY:
      return Status::kCapacityExceeded;
    case -EALREADY:
    case -ENETDOWN:
      return Status::kInvalidState;
    case -ETIMEDOUT:
      return Status::kTimeout;
    case -ENODEV:
    case -ENETUNREACH:
    case -ENOTSUP:
      return Status::kUnavailable;
    default:
      return Status::kInternal;
  }
}

std::uint32_t ReadCounter(const atomic_t& counter) noexcept {
  return static_cast<std::uint32_t>(atomic_get(&counter));
}

}  // namespace

CanDeviceAdapter::CanDeviceAdapter(const device& can_device) noexcept : can_device_(can_device) {
  k_msgq_init(&receive_queue_, receive_storage_.data(), sizeof(QueuedFrame), kReceiveQueueDepth);
}

Status CanDeviceAdapter::Ready() const noexcept {
  return device_is_ready(&can_device_) ? Status::kOk : Status::kUnavailable;
}

Status CanDeviceAdapter::Start(transport::can::CanFrameReceiver receiver,
                               CanReceiveFilter filter) noexcept {
  if (running_) {
    return Status::kInvalidState;
  }
  if (receiver.receive == nullptr || filter.arbitration_id > CAN_STD_ID_MASK ||
      filter.mask > CAN_STD_ID_MASK) {
    return Status::kInvalidArgument;
  }
  if (!device_is_ready(&can_device_)) {
    return Status::kUnavailable;
  }

  k_msgq_purge(&receive_queue_);
  const auto start_status = FromZephyrError(can_start(&can_device_));
  if (!IsOk(start_status)) {
    return start_status;
  }

  const can_filter native_filter{
      .id = filter.arbitration_id,
      .mask = filter.mask,
      .flags = 0,
  };
  filter_id_ = can_add_rx_filter(&can_device_, Receive, this, &native_filter);
  if (filter_id_ < 0) {
    const auto filter_status = FromZephyrError(filter_id_);
    filter_id_ = -1;
    static_cast<void>(can_stop(&can_device_));
    return filter_status;
  }

  receiver_ = receiver;
  running_ = true;
  return Status::kOk;
}

Status CanDeviceAdapter::Send(const transport::can::CanFrame& frame,
                              const ExecutionContext& caller) noexcept {
  if (!running_) {
    return Status::kInvalidState;
  }
  if (caller.kind() == ExecutionKind::kInterrupt || frame.arbitration_id > CAN_STD_ID_MASK ||
      frame.size > transport::can::kClassicCanPayloadSize) {
    return Status::kInvalidArgument;
  }

  can_frame native_frame{
      .id = frame.arbitration_id,
      .dlc = frame.size,
      .flags = 0,
  };
  for (std::size_t index = 0; index < frame.size; ++index) {
    native_frame.data[index] = std::to_integer<std::uint8_t>(frame.data[index]);
  }
  const auto status =
      FromZephyrError(can_send(&can_device_, &native_frame, K_NO_WAIT, TransmitComplete, this));
  if (IsOk(status)) {
    atomic_inc(&tx_frames_);
  } else {
    atomic_inc(&tx_failures_);
  }
  return status;
}

Status CanDeviceAdapter::Poll(const ExecutionContext& caller) noexcept {
  if (!running_) {
    return Status::kInvalidState;
  }
  if (caller.kind() == ExecutionKind::kInterrupt) {
    return Status::kInvalidArgument;
  }

  bool received{};
  Status result{Status::kOk};
  for (std::size_t index = 0; index < kReceiveQueueDepth; ++index) {
    QueuedFrame queued;
    if (k_msgq_get(&receive_queue_, &queued, K_NO_WAIT) != 0) {
      break;
    }
    received = true;
    if (queued.frame.id > CAN_STD_ID_MASK ||
        queued.frame.dlc > transport::can::kClassicCanPayloadSize ||
        (queued.frame.flags &
         (CAN_FRAME_IDE | CAN_FRAME_RTR | CAN_FRAME_FDF | CAN_FRAME_BRS | CAN_FRAME_ESI)) != 0) {
      atomic_inc(&invalid_frames_);
      result = Status::kProtocolError;
      continue;
    }

    transport::can::CanFrame frame;
    frame.arbitration_id = static_cast<std::uint16_t>(queued.frame.id);
    frame.size = queued.frame.dlc;
    for (std::size_t byte = 0; byte < frame.size; ++byte) {
      frame.data[byte] = static_cast<std::byte>(queued.frame.data[byte]);
    }
    const auto status = receiver_.Accept(frame, queued.receive_time_ns, caller);
    if (!IsOk(status) && status != Status::kUnavailable) {
      atomic_inc(&receiver_failures_);
      result = status;
    }
  }
  return received ? result : Status::kUnavailable;
}

void CanDeviceAdapter::Stop() noexcept {
  if (!running_) {
    return;
  }
  can_remove_rx_filter(&can_device_, filter_id_);
  filter_id_ = -1;
  static_cast<void>(can_stop(&can_device_));
  k_msgq_purge(&receive_queue_);
  receiver_ = {};
  running_ = false;
}

transport::can::CanFrameWriter CanDeviceAdapter::writer() noexcept { return {Write, this}; }

CanDeviceStats CanDeviceAdapter::stats() const noexcept {
  return {
      .tx_frames = ReadCounter(tx_frames_),
      .tx_completions = ReadCounter(tx_completions_),
      .tx_failures = ReadCounter(tx_failures_),
      .rx_frames = ReadCounter(rx_frames_),
      .rx_dropped = ReadCounter(rx_dropped_),
      .invalid_frames = ReadCounter(invalid_frames_),
      .receiver_failures = ReadCounter(receiver_failures_),
  };
}

Status CanDeviceAdapter::Write(void* state, const transport::can::CanFrame& frame,
                               const ExecutionContext& caller) noexcept {
  return state == nullptr ? Status::kInvalidArgument
                          : static_cast<CanDeviceAdapter*>(state)->Send(frame, caller);
}

void CanDeviceAdapter::Receive(const device*, can_frame* frame, void* state) noexcept {
  if (frame == nullptr || state == nullptr) {
    return;
  }
  auto& self = *static_cast<CanDeviceAdapter*>(state);
  const QueuedFrame queued{
      .frame = *frame,
      .receive_time_ns = k_ticks_to_ns_floor64(k_uptime_ticks()),
  };
  if (k_msgq_put(&self.receive_queue_, &queued, K_NO_WAIT) == 0) {
    atomic_inc(&self.rx_frames_);
  } else {
    atomic_inc(&self.rx_dropped_);
  }
}

void CanDeviceAdapter::TransmitComplete(const device*, int error, void* state) noexcept {
  if (state == nullptr) {
    return;
  }
  auto& self = *static_cast<CanDeviceAdapter*>(state);
  if (error == 0) {
    atomic_inc(&self.tx_completions_);
  } else {
    atomic_inc(&self.tx_failures_);
  }
}

}  // namespace aster::platform::zephyr
