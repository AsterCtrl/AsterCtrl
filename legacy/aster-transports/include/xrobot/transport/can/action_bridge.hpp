#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "xrobot/runtime/action.hpp"
#include "xrobot/transport/can/service_bridge.hpp"

namespace xrobot::transport::can {

enum class ActionWireOperation : std::uint8_t {
  kGoal = 0,
  kCancel = 1,
  kGoalResponse = 2,
  kFeedback = 3,
  kResult = 4,
  kCancelResponse = 5,
};

namespace action_detail {

inline void WriteU32(std::uint32_t value, std::span<std::byte, 4> output) noexcept {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> (index * 8U));
  }
}

inline std::uint32_t ReadU32(std::span<const std::byte, 4> input) noexcept {
  std::uint32_t value{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
  }
  return value;
}

inline void WriteU64(std::uint64_t value, std::span<std::byte, 8> output) noexcept {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(value >> (index * 8U));
  }
}

inline std::uint64_t ReadU64(std::span<const std::byte, 8> input) noexcept {
  std::uint64_t value{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
  }
  return value;
}

inline bool SenderBusy(ReliableSenderState state) noexcept {
  return state == ReliableSenderState::kSending ||
         state == ReliableSenderState::kWaitingForAck;
}

inline bool DecodeStatus(std::byte encoded, Status& status) noexcept {
  const auto value = std::to_integer<std::uint8_t>(encoded);
  if (value > static_cast<std::uint8_t>(Status::kInternal)) {
    return false;
  }
  status = static_cast<Status>(value);
  return true;
}

}  // namespace action_detail

struct ActionClientTransportStats {
  std::uint32_t goals{};
  std::uint32_t feedback{};
  std::uint32_t results{};
  std::uint32_t cancels{};
  std::uint32_t rejected{};
  std::uint32_t decode_failures{};
  std::uint32_t send_failures{};
  std::uint32_t timeouts{};
};

template <xrobot::runtime::ActionType Action>
class CanActionClient final : public xrobot::runtime::ActionClientEndpoint<Action> {
 public:
  using Goal = xrobot::runtime::ActionGoal<Action>;
  using Feedback = xrobot::runtime::ActionFeedback<Action>;
  using Result = xrobot::runtime::ActionResult<Action>;
  static constexpr std::size_t kGoalSize =
      xrobot::runtime::TypeSupport<Goal>::descriptor().max_serialized_size;
  static constexpr std::size_t kFeedbackSize =
      xrobot::runtime::TypeSupport<Feedback>::descriptor().max_serialized_size;
  static constexpr std::size_t kResultSize =
      xrobot::runtime::TypeSupport<Result>::descriptor().max_serialized_size;
  static constexpr std::size_t kClientPayloadSize = 13U + kGoalSize;
  static constexpr std::size_t kServerPayloadSize =
      std::max({std::size_t{6}, 5U + kFeedbackSize, 6U + kResultSize});

  constexpr CanActionClient(std::uint16_t route_id, CanPriority priority,
                            CanFrameWriter writer, CanClockReader clock,
                            std::uint64_t retry_timeout_ns = 5'000'000,
                            std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        writer_(writer),
        clock_(clock),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status SendGoal(const Goal& goal, xrobot::runtime::ActionCallbacks<Action> callbacks,
                  std::uint64_t deadline_ns,
                  const xrobot::runtime::ExecutionContext& caller,
                  xrobot::runtime::ActionGoalHandle& handle) noexcept override {
    handle = {};
    if (active_ || callbacks.on_goal_response == nullptr ||
        callbacks.on_result == nullptr || clock_.read == nullptr) {
      ++stats_.rejected;
      return active_ ? Status::kCapacityExceeded : Status::kInvalidArgument;
    }
    ++next_goal_id_;
    if (next_goal_id_ == 0) {
      ++next_goal_id_;
    }
    handle = {next_goal_id_};
    outbound_[0] = static_cast<std::byte>(ActionWireOperation::kGoal);
    action_detail::WriteU32(handle.id,
                            std::span<std::byte>(outbound_).subspan<1, 4>());
    action_detail::WriteU64(deadline_ns,
                            std::span<std::byte>(outbound_).subspan<5, 8>());
    std::size_t written{};
    const auto encode_status = xrobot::runtime::TypeSupport<Goal>::Encode(
        goal, std::span<std::byte>(outbound_).subspan(13), written);
    if (encode_status != Status::kOk || written != kGoalSize) {
      ++stats_.rejected;
      handle = {};
      return encode_status == Status::kOk ? Status::kInternal : encode_status;
    }
    handle_ = handle;
    callbacks_ = callbacks;
    active_ = true;
    const auto status =
        SendOutbound(kClientPayloadSize, ActionWireOperation::kGoal, caller);
    if (status != Status::kOk) {
      ClearActive();
      handle = {};
      ++stats_.send_failures;
      return status;
    }
    ++stats_.goals;
    return Status::kOk;
  }

  Status Cancel(xrobot::runtime::ActionGoalHandle handle,
                const xrobot::runtime::ExecutionContext& caller) noexcept override {
    if (!active_ || handle != handle_ || action_detail::SenderBusy(sender_.state())) {
      return Status::kInvalidState;
    }
    outbound_[0] = static_cast<std::byte>(ActionWireOperation::kCancel);
    action_detail::WriteU32(handle.id,
                            std::span<std::byte>(outbound_).subspan<1, 4>());
    const auto status = SendOutbound(5, ActionWireOperation::kCancel, caller);
    if (status == Status::kOk) {
      ++stats_.cancels;
    } else {
      ++stats_.send_failures;
    }
    return status;
  }

  Status Accept(const CanFrame& frame, std::uint64_t,
                const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (frame.size == 0 || GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      const auto status = sender_.HandleAck(frame);
      if (status == Status::kOk) {
        pending_operation_ = PendingOperation::kNone;
      }
      return status;
    }
    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto receive_status = receiver_.Accept(frame, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (ack_status != Status::kOk) {
        ++stats_.send_failures;
        return ack_status;
      }
    }
    if (receive_status != Status::kOk) {
      return receive_status;
    }
    if (message.payload.size() < 5 || !active_) {
      return DecodeFailure();
    }
    const auto operation = static_cast<ActionWireOperation>(
        std::to_integer<std::uint8_t>(message.payload[0]));
    const auto token = action_detail::ReadU32(message.payload.subspan<1, 4>());
    if (token != handle_.id) {
      return DecodeFailure();
    }
    switch (operation) {
      case ActionWireOperation::kGoalResponse:
        return HandleGoalResponse(message, caller);
      case ActionWireOperation::kFeedback:
        return HandleFeedback(message, caller);
      case ActionWireOperation::kResult:
        return HandleResult(message, caller);
      case ActionWireOperation::kCancelResponse:
        return HandleCancelResponse(message, caller);
      default:
        return DecodeFailure();
    }
  }

  Status Poll(std::uint64_t now_ns,
              const xrobot::runtime::ExecutionContext& caller) noexcept {
    const auto status = sender_.Poll(now_ns);
    if (status == Status::kOk) {
      return Pump(caller);
    }
    if (status != Status::kTimeout) {
      return status;
    }

    const auto operation = pending_operation_;
    pending_operation_ = PendingOperation::kNone;
    ++stats_.timeouts;
    if (operation == PendingOperation::kGoal && active_) {
      const auto callback = callbacks_.on_goal_response;
      auto* const state = callbacks_.state;
      const auto handle = handle_;
      ClearActive();
      callback(state, handle, Status::kTimeout, caller);
    } else if (operation == PendingOperation::kCancel && active_ &&
               callbacks_.on_cancel_response != nullptr) {
      callbacks_.on_cancel_response(callbacks_.state, handle_, Status::kTimeout,
                                    caller);
    }
    return status;
  }

  xrobot::runtime::ActionClient<Action> client() noexcept {
    return xrobot::runtime::ActionClient<Action>(*this);
  }
  const ActionClientTransportStats& stats() const noexcept { return stats_; }

 private:
  enum class PendingOperation : std::uint8_t {
    kNone,
    kGoal,
    kCancel,
  };

  Status SendOutbound(std::size_t size, ActionWireOperation operation,
                      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (action_detail::SenderBusy(sender_.state())) {
      return Status::kCapacityExceeded;
    }
    const auto sequence = tx_sequence_;
    tx_sequence_ = static_cast<std::uint8_t>((tx_sequence_ + 1U) & 0x0fU);
    const auto status = sender_.Begin(
        route_id_, priority_, sequence,
        std::span<const std::byte>(outbound_.data(), size), clock_.NowNs(),
        retry_timeout_ns_, maximum_retries_);
    if (status != Status::kOk) {
      return status;
    }
    pending_operation_ = operation == ActionWireOperation::kGoal
                             ? PendingOperation::kGoal
                             : PendingOperation::kCancel;
    return Pump(caller);
  }

  Status Pump(const xrobot::runtime::ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (sender_.NextFrame(frame) == Status::kOk) {
      const auto status = writer_.Send(frame, caller);
      if (status != Status::kOk) {
        return status;
      }
    }
    return Status::kOk;
  }

  Status HandleGoalResponse(
      const ReassembledMessage& message,
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (message.payload.size() != 6) {
      return DecodeFailure();
    }
    Status status;
    if (!action_detail::DecodeStatus(message.payload[5], status)) {
      return DecodeFailure();
    }
    const auto callback = callbacks_.on_goal_response;
    auto* const state = callbacks_.state;
    const auto handle = handle_;
    if (status != Status::kOk) {
      ClearActive();
    }
    callback(state, handle, status, caller);
    return Status::kOk;
  }

  Status HandleFeedback(
      const ReassembledMessage& message,
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (message.payload.size() != 5U + kFeedbackSize) {
      return DecodeFailure();
    }
    Feedback feedback;
    const auto status = xrobot::runtime::TypeSupport<Feedback>::Decode(
        message.payload.subspan(5), feedback);
    if (status != Status::kOk) {
      return DecodeFailure();
    }
    if (callbacks_.on_feedback != nullptr) {
      callbacks_.on_feedback(callbacks_.state, handle_, feedback, caller);
    }
    ++stats_.feedback;
    return Status::kOk;
  }

  Status HandleResult(const ReassembledMessage& message,
                      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (message.payload.size() != 6U + kResultSize) {
      return DecodeFailure();
    }
    Status status;
    if (!action_detail::DecodeStatus(message.payload[5], status)) {
      return DecodeFailure();
    }
    Result result;
    const auto decode_status = xrobot::runtime::TypeSupport<Result>::Decode(
        message.payload.subspan(6), result);
    if (decode_status != Status::kOk) {
      return DecodeFailure();
    }
    const auto callback = callbacks_.on_result;
    auto* const state = callbacks_.state;
    const auto handle = handle_;
    ClearActive();
    ++stats_.results;
    callback(state, handle, status, result, caller);
    return Status::kOk;
  }

  Status HandleCancelResponse(
      const ReassembledMessage& message,
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (message.payload.size() != 6 || callbacks_.on_cancel_response == nullptr) {
      return DecodeFailure();
    }
    Status status;
    if (!action_detail::DecodeStatus(message.payload[5], status)) {
      return DecodeFailure();
    }
    callbacks_.on_cancel_response(callbacks_.state, handle_, status, caller);
    return Status::kOk;
  }

  Status DecodeFailure() noexcept {
    ++stats_.decode_failures;
    return Status::kInvalidArgument;
  }

  void ClearActive() noexcept {
    active_ = false;
    pending_operation_ = PendingOperation::kNone;
    handle_ = {};
    callbacks_ = {};
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  CanFrameWriter writer_;
  CanClockReader clock_;
  std::uint64_t retry_timeout_ns_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t tx_sequence_{};
  std::uint32_t next_goal_id_{};
  PendingOperation pending_operation_{PendingOperation::kNone};
  std::array<std::byte, kClientPayloadSize> outbound_{};
  ReliableSender<kClientPayloadSize> sender_;
  ReliableReceiver<kServerPayloadSize> receiver_;
  xrobot::runtime::ActionGoalHandle handle_{};
  xrobot::runtime::ActionCallbacks<Action> callbacks_{};
  bool active_{};
  ActionClientTransportStats stats_{};
};

struct ActionServerTransportStats {
  std::uint32_t goals{};
  std::uint32_t feedback{};
  std::uint32_t results{};
  std::uint32_t cancels{};
  std::uint32_t rejected{};
  std::uint32_t decode_failures{};
  std::uint32_t send_failures{};
};

template <xrobot::runtime::ActionType Action>
class CanActionServer {
 public:
  using Goal = xrobot::runtime::ActionGoal<Action>;
  using Feedback = xrobot::runtime::ActionFeedback<Action>;
  using Result = xrobot::runtime::ActionResult<Action>;
  static constexpr std::size_t kGoalSize =
      xrobot::runtime::TypeSupport<Goal>::descriptor().max_serialized_size;
  static constexpr std::size_t kFeedbackSize =
      xrobot::runtime::TypeSupport<Feedback>::descriptor().max_serialized_size;
  static constexpr std::size_t kResultSize =
      xrobot::runtime::TypeSupport<Result>::descriptor().max_serialized_size;
  static constexpr std::size_t kClientPayloadSize = 13U + kGoalSize;
  static constexpr std::size_t kServerPayloadSize =
      std::max({std::size_t{6}, 5U + kFeedbackSize, 6U + kResultSize});

  constexpr CanActionServer(std::uint16_t route_id, CanPriority priority,
                            xrobot::runtime::ActionClient<Action> local_action,
                            CanFrameWriter writer, CanClockReader clock,
                            std::uint64_t retry_timeout_ns = 5'000'000,
                            std::uint8_t maximum_retries = 2) noexcept
      : route_id_(route_id),
        priority_(priority),
        local_action_(local_action),
        writer_(writer),
        clock_(clock),
        retry_timeout_ns_(retry_timeout_ns),
        maximum_retries_(maximum_retries) {}

  Status Accept(const CanFrame& frame, std::uint64_t,
                const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (frame.size == 0 || GetFrameKind(frame.data[0]) != FrameKind::kReliable) {
      return Status::kInvalidArgument;
    }
    if (GetReliableSubtype(frame.data[0]) == ReliableSubtype::kAck) {
      return sender_.HandleAck(frame);
    }
    ReassembledMessage message;
    CanFrame acknowledgement;
    const auto receive_status = receiver_.Accept(frame, message, acknowledgement);
    if (acknowledgement.size != 0) {
      const auto ack_status = writer_.Send(acknowledgement, caller);
      if (ack_status != Status::kOk) {
        ++stats_.send_failures;
        return ack_status;
      }
    }
    if (receive_status != Status::kOk) {
      return receive_status;
    }
    if (message.payload.size() < 5) {
      return DecodeFailure();
    }
    const auto operation = static_cast<ActionWireOperation>(
        std::to_integer<std::uint8_t>(message.payload[0]));
    const auto token = action_detail::ReadU32(message.payload.subspan<1, 4>());
    if (operation == ActionWireOperation::kGoal) {
      return HandleGoal(token, message, caller);
    }
    if (operation == ActionWireOperation::kCancel) {
      return HandleCancel(token, message, caller);
    }
    return DecodeFailure();
  }

  Status Poll(std::uint64_t now_ns,
              const xrobot::runtime::ExecutionContext& caller) noexcept {
    const auto status = sender_.Poll(now_ns);
    return status == Status::kOk ? Pump(caller) : status;
  }

  const ActionServerTransportStats& stats() const noexcept { return stats_; }

 private:
  static void GoalResponseThunk(
      void* state, xrobot::runtime::ActionGoalHandle, Status status,
      const xrobot::runtime::ExecutionContext& context) noexcept {
    auto& self = *static_cast<CanActionServer*>(state);
    if (self.SendStatus(ActionWireOperation::kGoalResponse, status, context) !=
        Status::kOk) {
      ++self.stats_.send_failures;
    }
    if (status != Status::kOk) {
      self.active_ = false;
    }
  }

  static void FeedbackThunk(
      void* state, xrobot::runtime::ActionGoalHandle,
      const Feedback& feedback,
      const xrobot::runtime::ExecutionContext& context) noexcept {
    auto& self = *static_cast<CanActionServer*>(state);
    self.outbound_[0] = static_cast<std::byte>(ActionWireOperation::kFeedback);
    self.WriteToken();
    std::size_t written{};
    const auto status = xrobot::runtime::TypeSupport<Feedback>::Encode(
        feedback, std::span<std::byte>(self.outbound_).subspan(5), written);
    if (status != Status::kOk || written != kFeedbackSize ||
        self.SendOutbound(5U + kFeedbackSize, context) != Status::kOk) {
      ++self.stats_.send_failures;
      return;
    }
    ++self.stats_.feedback;
  }

  static void ResultThunk(
      void* state, xrobot::runtime::ActionGoalHandle, Status status,
      const Result& result,
      const xrobot::runtime::ExecutionContext& context) noexcept {
    auto& self = *static_cast<CanActionServer*>(state);
    self.outbound_[0] = static_cast<std::byte>(ActionWireOperation::kResult);
    self.WriteToken();
    self.outbound_[5] = static_cast<std::byte>(status);
    std::size_t written{};
    const auto encode_status = xrobot::runtime::TypeSupport<Result>::Encode(
        result, std::span<std::byte>(self.outbound_).subspan(6), written);
    self.active_ = false;
    if (encode_status != Status::kOk || written != kResultSize ||
        self.SendOutbound(6U + kResultSize, context) != Status::kOk) {
      ++self.stats_.send_failures;
      return;
    }
    ++self.stats_.results;
  }

  static void CancelResponseThunk(
      void* state, xrobot::runtime::ActionGoalHandle, Status status,
      const xrobot::runtime::ExecutionContext& context) noexcept {
    auto& self = *static_cast<CanActionServer*>(state);
    if (self.SendStatus(ActionWireOperation::kCancelResponse, status, context) !=
        Status::kOk) {
      ++self.stats_.send_failures;
    }
  }

  Status HandleGoal(std::uint32_t token, const ReassembledMessage& message,
                    const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (message.payload.size() != kClientPayloadSize || active_) {
      ++stats_.rejected;
      return active_ ? SendStatusForToken(
                           token, ActionWireOperation::kGoalResponse,
                           Status::kCapacityExceeded, caller)
                     : DecodeFailure();
    }
    Goal goal;
    const auto status = xrobot::runtime::TypeSupport<Goal>::Decode(
        message.payload.subspan(13), goal);
    if (status != Status::kOk) {
      return DecodeFailure();
    }
    remote_token_ = token;
    active_ = true;
    const auto deadline = action_detail::ReadU64(message.payload.subspan<5, 8>());
    xrobot::runtime::ActionCallbacks<Action> callbacks{
        GoalResponseThunk, FeedbackThunk, ResultThunk, CancelResponseThunk, this};
    const auto call_status = local_action_.SendGoal(
        goal, callbacks, deadline, caller, local_handle_);
    if (call_status != Status::kOk) {
      active_ = false;
      ++stats_.rejected;
      return SendStatusForToken(token, ActionWireOperation::kGoalResponse,
                                call_status, caller);
    }
    ++stats_.goals;
    return Status::kOk;
  }

  Status HandleCancel(std::uint32_t token, const ReassembledMessage& message,
                      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (message.payload.size() != 5 || !active_ || token != remote_token_) {
      return SendStatusForToken(token, ActionWireOperation::kCancelResponse,
                                Status::kInvalidState, caller);
    }
    const auto status = local_action_.Cancel(local_handle_, caller);
    if (status == Status::kOk) {
      ++stats_.cancels;
      return status;
    }
    return SendStatus(ActionWireOperation::kCancelResponse, status, caller);
  }

  Status SendStatus(ActionWireOperation operation, Status status,
                    const xrobot::runtime::ExecutionContext& caller) noexcept {
    return SendStatusForToken(remote_token_, operation, status, caller);
  }

  Status SendStatusForToken(
      std::uint32_t token, ActionWireOperation operation, Status status,
      const xrobot::runtime::ExecutionContext& caller) noexcept {
    outbound_[0] = static_cast<std::byte>(operation);
    action_detail::WriteU32(token,
                            std::span<std::byte>(outbound_).subspan<1, 4>());
    outbound_[5] = static_cast<std::byte>(status);
    return SendOutbound(6, caller);
  }

  void WriteToken() noexcept {
    action_detail::WriteU32(remote_token_,
                            std::span<std::byte>(outbound_).subspan<1, 4>());
  }

  Status SendOutbound(std::size_t size,
                      const xrobot::runtime::ExecutionContext& caller) noexcept {
    if (action_detail::SenderBusy(sender_.state())) {
      return Status::kCapacityExceeded;
    }
    const auto sequence = tx_sequence_;
    tx_sequence_ = static_cast<std::uint8_t>((tx_sequence_ + 1U) & 0x0fU);
    const auto status = sender_.Begin(
        route_id_, priority_, sequence,
        std::span<const std::byte>(outbound_.data(), size), clock_.NowNs(),
        retry_timeout_ns_, maximum_retries_);
    return status == Status::kOk ? Pump(caller) : status;
  }

  Status Pump(const xrobot::runtime::ExecutionContext& caller) noexcept {
    CanFrame frame;
    while (sender_.NextFrame(frame) == Status::kOk) {
      const auto status = writer_.Send(frame, caller);
      if (status != Status::kOk) {
        return status;
      }
    }
    return Status::kOk;
  }

  Status DecodeFailure() noexcept {
    ++stats_.decode_failures;
    return Status::kInvalidArgument;
  }

  std::uint16_t route_id_{};
  CanPriority priority_{CanPriority::kBackground};
  xrobot::runtime::ActionClient<Action> local_action_;
  CanFrameWriter writer_;
  CanClockReader clock_;
  std::uint64_t retry_timeout_ns_{};
  std::uint8_t maximum_retries_{};
  std::uint8_t tx_sequence_{};
  std::uint32_t remote_token_{};
  std::array<std::byte, kServerPayloadSize> outbound_{};
  ReliableReceiver<kClientPayloadSize> receiver_;
  ReliableSender<kServerPayloadSize> sender_;
  xrobot::runtime::ActionGoalHandle local_handle_{};
  bool active_{};
  ActionServerTransportStats stats_{};
};

}  // namespace xrobot::transport::can
