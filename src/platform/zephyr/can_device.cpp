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

bool IsTransmitAbort(int error) noexcept { return error == -ENETDOWN || error == -ECANCELED; }

}  // namespace

CanDeviceAdapter::CanDeviceAdapter(const device& can_device) noexcept : can_device_(can_device) {
  k_mutex_init(&control_mutex_);
  k_mutex_init(&poll_mutex_);
  k_sem_init(&callback_finished_, 0, 1);
  k_msgq_init(&receive_queue_, receive_storage_.data(), sizeof(QueuedFrame), kReceiveQueueDepth);
  k_msgq_init(&transmit_queue_, transmit_storage_.data(), sizeof(can_frame), kTransmitQueueDepth);
}

Status CanDeviceAdapter::Ready() const noexcept {
  return device_is_ready(&can_device_) ? Status::kOk : Status::kUnavailable;
}

Status CanDeviceAdapter::Start(transport::can::CanFrameReceiver receiver,
                               CanReceiveFilter filter) noexcept {
  if (receiver.receive == nullptr || filter.arbitration_id > CAN_STD_ID_MASK ||
      filter.mask > CAN_STD_ID_MASK) {
    return Status::kInvalidArgument;
  }
  if (k_is_in_isr()) {
    return Status::kInvalidArgument;
  }

  static_cast<void>(k_mutex_lock(&control_mutex_, K_FOREVER));
  if (LoadState() != CanDeviceState::kStopped) {
    k_mutex_unlock(&control_mutex_);
    return Status::kInvalidState;
  }
  if (!device_is_ready(&can_device_)) {
    k_mutex_unlock(&control_mutex_);
    return Status::kUnavailable;
  }

  StoreState(CanDeviceState::kStarting);
  k_msgq_purge(&receive_queue_);
  k_msgq_purge(&transmit_queue_);
  atomic_clear(&tx_in_flight_);
  receiver_ = receiver;

  const can_filter native_filter{
      .id = filter.arbitration_id,
      .mask = filter.mask,
      .flags = 0,
  };
  filter_id_ = can_add_rx_filter(&can_device_, Receive, this, &native_filter);
  if (filter_id_ < 0) {
    const auto filter_status = FromZephyrError(filter_id_);
    filter_id_ = -1;
    receiver_ = {};
    StoreState(CanDeviceState::kStopped);
    k_mutex_unlock(&control_mutex_);
    return filter_status;
  }

  const auto start_status = FromZephyrError(can_start(&can_device_));
  if (!IsOk(start_status)) {
    can_remove_rx_filter(&can_device_, filter_id_);
    filter_id_ = -1;
    WaitForCallbacksLocked();
    receiver_ = {};
    StoreState(CanDeviceState::kStopped);
    k_mutex_unlock(&control_mutex_);
    return start_status;
  }

  StoreState(CanDeviceState::kRunning);
  k_mutex_unlock(&control_mutex_);
  return Status::kOk;
}

Status CanDeviceAdapter::Send(const transport::can::CanFrame& frame,
                              const ExecutionContext& caller) noexcept {
  if (k_is_in_isr() || caller.kind() == ExecutionKind::kInterrupt ||
      frame.arbitration_id > CAN_STD_ID_MASK ||
      frame.size > transport::can::kClassicCanPayloadSize) {
    return Status::kInvalidArgument;
  }

  static_cast<void>(k_mutex_lock(&control_mutex_, K_FOREVER));
  if (LoadState() != CanDeviceState::kRunning) {
    k_mutex_unlock(&control_mutex_);
    return Status::kInvalidState;
  }

  can_frame native_frame{
      .id = frame.arbitration_id,
      .dlc = frame.size,
      .flags = 0,
  };
  for (std::size_t index = 0; index < frame.size; ++index) {
    native_frame.data[index] = std::to_integer<std::uint8_t>(frame.data[index]);
  }

  if (k_msgq_put(&transmit_queue_, &native_frame, K_NO_WAIT) != 0) {
    atomic_inc(&tx_rejected_);
    k_mutex_unlock(&control_mutex_);
    return Status::kCapacityExceeded;
  }
  atomic_inc(&tx_frames_);
  PumpTransmitLocked();
  k_mutex_unlock(&control_mutex_);
  return Status::kOk;
}

Status CanDeviceAdapter::Poll(const ExecutionContext& caller) noexcept {
  if (k_is_in_isr() || caller.kind() == ExecutionKind::kInterrupt) {
    return Status::kInvalidArgument;
  }

  static_cast<void>(k_mutex_lock(&poll_mutex_, K_FOREVER));
  static_cast<void>(k_mutex_lock(&control_mutex_, K_FOREVER));
  if (LoadState() != CanDeviceState::kRunning) {
    k_mutex_unlock(&control_mutex_);
    k_mutex_unlock(&poll_mutex_);
    return Status::kInvalidState;
  }
  PumpTransmitLocked();

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
    const auto receiver = receiver_;
    dispatch_thread_id_ = k_current_get();
    k_mutex_unlock(&control_mutex_);
    const auto status = receiver.Accept(frame, queued.receive_time_ns, caller);
    static_cast<void>(k_mutex_lock(&control_mutex_, K_FOREVER));
    dispatch_thread_id_ = nullptr;
    if (!IsOk(status) && status != Status::kUnavailable) {
      atomic_inc(&receiver_failures_);
      result = status;
    }
    if (LoadState() != CanDeviceState::kRunning) {
      break;
    }
  }
  PumpTransmitLocked();
  k_mutex_unlock(&control_mutex_);
  k_mutex_unlock(&poll_mutex_);
  return received ? result : Status::kUnavailable;
}

Status CanDeviceAdapter::Stop() noexcept {
  if (k_is_in_isr()) {
    return Status::kInvalidArgument;
  }

  static_cast<void>(k_mutex_lock(&control_mutex_, K_FOREVER));
  const auto prior_state = LoadState();
  if (prior_state == CanDeviceState::kStopped) {
    k_mutex_unlock(&control_mutex_);
    return Status::kOk;
  }
  if (prior_state != CanDeviceState::kRunning && prior_state != CanDeviceState::kStopFailed) {
    k_mutex_unlock(&control_mutex_);
    return Status::kInvalidState;
  }
  const bool called_from_dispatch = dispatch_thread_id_ == k_current_get();

  StoreState(CanDeviceState::kStopping);
  AbortQueuedTransmitsLocked();

  // A successful can_stop() settles every driver-owned TX mailbox. Keep the
  // filter and callback state intact when it fails so Stop() can be retried.
  const int stop_error = can_stop(&can_device_);
  if (stop_error != 0 && stop_error != -EALREADY) {
    StoreState(CanDeviceState::kStopFailed);
    k_mutex_unlock(&control_mutex_);
    return FromZephyrError(stop_error);
  }

  if (filter_id_ >= 0) {
    can_remove_rx_filter(&can_device_, filter_id_);
    filter_id_ = -1;
  }

  // Do not retain the state lock while another Poll() receiver is running.
  // The current dispatch thread already owns poll_mutex_ and may finish the
  // stop in place; an external caller waits until that dispatch returns.
  if (!called_from_dispatch) {
    k_mutex_unlock(&control_mutex_);
    static_cast<void>(k_mutex_lock(&poll_mutex_, K_FOREVER));
    static_cast<void>(k_mutex_lock(&control_mutex_, K_FOREVER));
  }
  WaitForCallbacksLocked();
  if (atomic_cas(&tx_in_flight_, 1, 0)) {
    atomic_inc(&tx_aborted_);
  }
  k_msgq_purge(&receive_queue_);
  receiver_ = {};
  StoreState(CanDeviceState::kStopped);
  k_mutex_unlock(&control_mutex_);
  if (!called_from_dispatch) {
    k_mutex_unlock(&poll_mutex_);
  }
  return Status::kOk;
}

transport::can::CanFrameWriter CanDeviceAdapter::writer() noexcept { return {Write, this}; }

CanDeviceStats CanDeviceAdapter::stats() const noexcept {
  return {
      .tx_frames = ReadCounter(tx_frames_),
      .tx_completions = ReadCounter(tx_completions_),
      .tx_failures = ReadCounter(tx_failures_),
      .tx_aborted = ReadCounter(tx_aborted_),
      .tx_rejected = ReadCounter(tx_rejected_),
      .rx_frames = ReadCounter(rx_frames_),
      .rx_dropped = ReadCounter(rx_dropped_),
      .invalid_frames = ReadCounter(invalid_frames_),
      .receiver_failures = ReadCounter(receiver_failures_),
  };
}

CanDeviceState CanDeviceAdapter::state() const noexcept { return LoadState(); }

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
  atomic_inc(&self.callbacks_active_);
  if (self.LoadState() != CanDeviceState::kRunning) {
    atomic_inc(&self.rx_dropped_);
    atomic_dec(&self.callbacks_active_);
    k_sem_give(&self.callback_finished_);
    return;
  }
  const QueuedFrame queued{
      .frame = *frame,
      .receive_time_ns = k_ticks_to_ns_floor64(k_uptime_ticks()),
  };
  if (k_msgq_put(&self.receive_queue_, &queued, K_NO_WAIT) == 0) {
    atomic_inc(&self.rx_frames_);
  } else {
    atomic_inc(&self.rx_dropped_);
  }
  atomic_dec(&self.callbacks_active_);
  k_sem_give(&self.callback_finished_);
}

void CanDeviceAdapter::TransmitComplete(const device*, int error, void* state) noexcept {
  if (state == nullptr) {
    return;
  }
  auto& self = *static_cast<CanDeviceAdapter*>(state);
  atomic_inc(&self.callbacks_active_);
  if (atomic_cas(&self.tx_in_flight_, 1, 0)) {
    if (error == 0) {
      atomic_inc(&self.tx_completions_);
    } else if (IsTransmitAbort(error)) {
      atomic_inc(&self.tx_aborted_);
    } else {
      atomic_inc(&self.tx_failures_);
    }
  }
  atomic_dec(&self.callbacks_active_);
  k_sem_give(&self.callback_finished_);
  // Completion may run in an ISR. The next thread-context Send() or Poll()
  // advances the FIFO.
}

void CanDeviceAdapter::PumpTransmitLocked() noexcept {
  while (LoadState() == CanDeviceState::kRunning && atomic_get(&tx_in_flight_) == 0) {
    can_frame next_frame;
    if (k_msgq_peek(&transmit_queue_, &next_frame) != 0) {
      return;
    }

    atomic_set(&tx_in_flight_, 1);
    const int send_error = can_send(&can_device_, &next_frame, K_NO_WAIT, TransmitComplete, this);
    if (send_error == 0) {
      can_frame accepted_frame;
      static_cast<void>(k_msgq_get(&transmit_queue_, &accepted_frame, K_NO_WAIT));
      continue;
    }

    atomic_clear(&tx_in_flight_);
    if (send_error == -EAGAIN || send_error == -ENOSPC) {
      // The controller had no mailbox. k_msgq_peek() left this FIFO head in
      // place so a later Pump cannot skip or truncate it.
      return;
    }

    can_frame rejected_frame;
    static_cast<void>(k_msgq_get(&transmit_queue_, &rejected_frame, K_NO_WAIT));
    if (IsTransmitAbort(send_error)) {
      atomic_inc(&tx_aborted_);
    } else {
      atomic_inc(&tx_failures_);
    }
    return;
  }
}

void CanDeviceAdapter::AbortQueuedTransmitsLocked() noexcept {
  can_frame discarded_frame;
  while (k_msgq_get(&transmit_queue_, &discarded_frame, K_NO_WAIT) == 0) {
    atomic_inc(&tx_aborted_);
  }
}

void CanDeviceAdapter::WaitForCallbacksLocked() noexcept {
  while (atomic_get(&callbacks_active_) != 0) {
    static_cast<void>(k_sem_take(&callback_finished_, K_FOREVER));
  }
}

CanDeviceState CanDeviceAdapter::LoadState() const noexcept {
  return static_cast<CanDeviceState>(atomic_get(&state_));
}

void CanDeviceAdapter::StoreState(CanDeviceState state) noexcept {
  atomic_set(&state_, static_cast<atomic_val_t>(state));
}

}  // namespace aster::platform::zephyr
