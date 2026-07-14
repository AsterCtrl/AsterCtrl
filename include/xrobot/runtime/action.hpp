#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "xrobot/runtime/executor.hpp"
#include "xrobot/runtime/status.hpp"
#include "xrobot/runtime/type_support.hpp"

namespace xrobot::runtime {

struct ActionDescriptor {
  std::string_view name;
  SchemaHash schema_hash;
};

template <typename Action>
struct ActionTypeSupport;

template <typename Action>
concept ActionType =
    requires {
      typename ActionTypeSupport<Action>::Goal;
      typename ActionTypeSupport<Action>::Feedback;
      typename ActionTypeSupport<Action>::Result;
      { ActionTypeSupport<Action>::descriptor() } ->
          std::same_as<ActionDescriptor>;
    } &&
    MessageType<typename ActionTypeSupport<Action>::Goal> &&
    MessageType<typename ActionTypeSupport<Action>::Feedback> &&
    MessageType<typename ActionTypeSupport<Action>::Result>;

template <ActionType Action>
using ActionGoal = typename ActionTypeSupport<Action>::Goal;

template <ActionType Action>
using ActionFeedback = typename ActionTypeSupport<Action>::Feedback;

template <ActionType Action>
using ActionResult = typename ActionTypeSupport<Action>::Result;

struct ActionGoalHandle {
  std::uint32_t id{};

  constexpr explicit operator bool() const noexcept { return id != 0; }
  constexpr bool operator==(const ActionGoalHandle&) const noexcept = default;
};

template <ActionType Action>
struct ActionCallbacks {
  using GoalResponse = void (*)(void*, ActionGoalHandle, Status,
                                const ExecutionContext&) noexcept;
  using Feedback = void (*)(void*, ActionGoalHandle,
                            const ActionFeedback<Action>&,
                            const ExecutionContext&) noexcept;
  using Result = void (*)(void*, ActionGoalHandle, Status,
                          const ActionResult<Action>&,
                          const ExecutionContext&) noexcept;
  using CancelResponse = void (*)(void*, ActionGoalHandle, Status,
                                  const ExecutionContext&) noexcept;

  GoalResponse on_goal_response{};
  Feedback on_feedback{};
  Result on_result{};
  CancelResponse on_cancel_response{};
  void* state{};
};

template <ActionType Action>
using ActionGoalHandler =
    Status (*)(void*, const ActionGoal<Action>&, ActionGoalHandle,
               const ExecutionContext&) noexcept;

template <ActionType Action>
using ActionCancelHandler =
    Status (*)(void*, ActionGoalHandle, const ExecutionContext&) noexcept;

struct ActionStats {
  std::uint32_t goals_submitted{};
  std::uint32_t goals_accepted{};
  std::uint32_t goals_rejected{};
  std::uint32_t feedback_sent{};
  std::uint32_t goals_completed{};
  std::uint32_t cancel_requests{};
  std::uint32_t timeouts{};
  std::uint32_t schedule_failures{};
  std::size_t high_watermark{};
};

template <ActionType Action>
class ActionClientEndpoint {
 public:
  virtual ~ActionClientEndpoint() = default;

  virtual Status SendGoal(const ActionGoal<Action>& goal,
                          ActionCallbacks<Action> callbacks,
                          std::uint64_t deadline_ns,
                          const ExecutionContext& caller,
                          ActionGoalHandle& handle) noexcept = 0;
  virtual Status Cancel(ActionGoalHandle handle,
                        const ExecutionContext& caller) noexcept = 0;
};

template <ActionType Action>
class ActionClient {
 public:
  constexpr ActionClient() noexcept = default;
  constexpr explicit ActionClient(
      ActionClientEndpoint<Action>& endpoint) noexcept
      : endpoint_(&endpoint) {}

  Status SendGoal(const ActionGoal<Action>& goal,
                  ActionCallbacks<Action> callbacks,
                  std::uint64_t deadline_ns,
                  const ExecutionContext& caller,
                  ActionGoalHandle& handle) const noexcept {
    if (endpoint_ == nullptr) {
      handle = {};
      return Status::kUnavailable;
    }
    return endpoint_->SendGoal(goal, callbacks, deadline_ns, caller, handle);
  }

  Status Cancel(ActionGoalHandle handle,
                const ExecutionContext& caller) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->Cancel(handle, caller);
  }

 private:
  ActionClientEndpoint<Action>* endpoint_{};
};

template <ActionType Action>
class ActionServerEndpoint {
 public:
  virtual ~ActionServerEndpoint() = default;

  virtual Status BindHandlers(ActionGoalHandler<Action> goal_handler,
                              ActionCancelHandler<Action> cancel_handler,
                              void* handler_state) noexcept = 0;

  virtual Status PublishFeedback(
      ActionGoalHandle handle, const ActionFeedback<Action>& feedback,
      const ExecutionContext& caller) noexcept = 0;
  virtual Status Finish(ActionGoalHandle handle, Status status,
                        const ActionResult<Action>& result,
                        const ExecutionContext& caller) noexcept = 0;
};

template <ActionType Action>
class ActionServer {
 public:
  constexpr ActionServer() noexcept = default;
  constexpr explicit ActionServer(
      ActionServerEndpoint<Action>& endpoint) noexcept
      : endpoint_(&endpoint) {}

  Status BindHandlers(ActionGoalHandler<Action> goal_handler,
                      ActionCancelHandler<Action> cancel_handler,
                      void* handler_state) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->BindHandlers(goal_handler, cancel_handler, handler_state);
  }

  Status PublishFeedback(ActionGoalHandle handle,
                         const ActionFeedback<Action>& feedback,
                         const ExecutionContext& caller) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->PublishFeedback(handle, feedback, caller);
  }

  Status Finish(ActionGoalHandle handle, Status status,
                const ActionResult<Action>& result,
                const ExecutionContext& caller) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->Finish(handle, status, result, caller);
  }

 private:
  ActionServerEndpoint<Action>* endpoint_{};
};

template <ActionType Action, std::size_t MaxGoals>
class StaticAction final : public ActionClientEndpoint<Action>,
                           public ActionServerEndpoint<Action> {
 public:
  static_assert(MaxGoals > 0);

  using Goal = ActionGoal<Action>;
  using Feedback = ActionFeedback<Action>;
  using Result = ActionResult<Action>;
  using GoalHandler = ActionGoalHandler<Action>;
  using CancelHandler = ActionCancelHandler<Action>;

  StaticAction(std::string_view name, Executor& executor) noexcept
      : name_(name), executor_(executor) {
    for (auto& slot : slots_) {
      slot.owner = this;
    }
  }

  StaticAction(std::string_view name, Executor& executor,
               GoalHandler goal_handler, CancelHandler cancel_handler,
               void* handler_state) noexcept
      : StaticAction(name, executor) {
    goal_handler_ = goal_handler;
    cancel_handler_ = cancel_handler;
    handler_state_ = handler_state;
  }

  Status BindHandlers(GoalHandler goal_handler, CancelHandler cancel_handler,
                      void* handler_state) noexcept override {
    if (goal_handler == nullptr) {
      return Status::kInvalidArgument;
    }
    if (goal_handler_ != nullptr || active_goals_ != 0) {
      return Status::kInvalidState;
    }
    goal_handler_ = goal_handler;
    cancel_handler_ = cancel_handler;
    handler_state_ = handler_state;
    return Status::kOk;
  }

  Status SendGoal(const Goal& goal, ActionCallbacks<Action> callbacks,
                  std::uint64_t deadline_ns,
                  const ExecutionContext& caller,
                  ActionGoalHandle& handle) noexcept override {
    handle = {};
    if (name_.empty() || goal_handler_ == nullptr ||
        callbacks.on_goal_response == nullptr || callbacks.on_result == nullptr) {
      return Status::kInvalidArgument;
    }

    Slot* available = nullptr;
    for (auto& slot : slots_) {
      if (slot.state == GoalState::kFree) {
        available = &slot;
        break;
      }
    }
    if (available == nullptr) {
      ++stats_.goals_rejected;
      return Status::kCapacityExceeded;
    }

    ++next_goal_id_;
    if (next_goal_id_ == 0) {
      ++next_goal_id_;
    }
    handle = ActionGoalHandle{next_goal_id_};
    available->goal = goal;
    available->handle = handle;
    available->callbacks = callbacks;
    available->deadline_ns = deadline_ns;
    available->cancel_enqueued = false;
    available->state = GoalState::kPending;
    ++active_goals_;
    if (active_goals_ > stats_.high_watermark) {
      stats_.high_watermark = active_goals_;
    }

    const auto status = executor_.TryPost({AcceptThunk, available}, caller);
    if (!IsOk(status)) {
      ++stats_.schedule_failures;
      Release(*available);
      handle = {};
      return status;
    }
    ++stats_.goals_submitted;
    return Status::kOk;
  }

  Status Cancel(ActionGoalHandle handle,
                const ExecutionContext& caller) noexcept override {
    auto* slot = Find(handle);
    if (slot == nullptr) {
      return Status::kInvalidArgument;
    }
    if (slot->state != GoalState::kActive || slot->cancel_enqueued) {
      return Status::kInvalidState;
    }
    if (cancel_handler_ == nullptr ||
        slot->callbacks.on_cancel_response == nullptr) {
      return Status::kUnavailable;
    }

    slot->cancel_enqueued = true;
    const auto status = executor_.TryPost({CancelThunk, slot}, caller);
    if (!IsOk(status)) {
      slot->cancel_enqueued = false;
      ++stats_.schedule_failures;
      return status;
    }
    ++stats_.cancel_requests;
    return Status::kOk;
  }

  Status PublishFeedback(ActionGoalHandle handle, const Feedback& feedback,
                         const ExecutionContext& caller) noexcept override {
    if (caller.kind() == ExecutionKind::kInterrupt) {
      return Status::kInvalidArgument;
    }
    auto* slot = Find(handle);
    if (slot == nullptr || (slot->state != GoalState::kActive &&
                            slot->state != GoalState::kCancelRequested)) {
      return Status::kInvalidState;
    }
    if (slot->callbacks.on_feedback != nullptr) {
      slot->callbacks.on_feedback(slot->callbacks.state, handle, feedback,
                                  caller);
    }
    ++stats_.feedback_sent;
    return Status::kOk;
  }

  Status Finish(ActionGoalHandle handle, Status status, const Result& result,
                const ExecutionContext& caller) noexcept override {
    if (caller.kind() == ExecutionKind::kInterrupt) {
      return Status::kInvalidArgument;
    }
    auto* slot = Find(handle);
    if (slot == nullptr || slot->cancel_enqueued ||
        (slot->state != GoalState::kActive &&
         slot->state != GoalState::kCancelRequested)) {
      return Status::kInvalidState;
    }

    const auto callback = slot->callbacks.on_result;
    auto* const callback_state = slot->callbacks.state;
    Release(*slot);
    ++stats_.goals_completed;
    callback(callback_state, handle, status, result, caller);
    return Status::kOk;
  }

  std::size_t ExpireDeadlines(
      std::uint64_t now_ns, const ExecutionContext& caller) noexcept {
    if (caller.kind() == ExecutionKind::kInterrupt) {
      return 0;
    }

    std::size_t expired{};
    for (auto& slot : slots_) {
      if ((slot.state == GoalState::kActive ||
           slot.state == GoalState::kCancelRequested) &&
          !slot.cancel_enqueued && slot.deadline_ns != 0 &&
          slot.deadline_ns <= now_ns) {
        const auto handle = slot.handle;
        if (IsOk(Finish(handle, Status::kTimeout, Result{}, caller))) {
          ++stats_.timeouts;
          ++expired;
        }
      }
    }
    return expired;
  }

  ActionClient<Action> client() noexcept {
    return ActionClient<Action>(*this);
  }
  ActionServer<Action> server() noexcept {
    return ActionServer<Action>(*this);
  }
  std::size_t active_goals() const noexcept { return active_goals_; }
  const ActionStats& stats() const noexcept { return stats_; }

 private:
  enum class GoalState : std::uint8_t {
    kFree,
    kPending,
    kActive,
    kCancelRequested,
  };

  struct Slot {
    StaticAction* owner{};
    Goal goal{};
    ActionGoalHandle handle{};
    ActionCallbacks<Action> callbacks{};
    std::uint64_t deadline_ns{};
    GoalState state{GoalState::kFree};
    bool cancel_enqueued{};
  };

  static void AcceptThunk(void* state,
                          const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<Slot*>(state);
    slot.owner->Accept(slot, context);
  }

  static void CancelThunk(void* state,
                          const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<Slot*>(state);
    slot.owner->HandleCancel(slot, context);
  }

  void Accept(Slot& slot, const ExecutionContext& context) noexcept {
    if (slot.state != GoalState::kPending) {
      return;
    }
    const auto status =
        goal_handler_(handler_state_, slot.goal, slot.handle, context);
    const auto handle = slot.handle;
    const auto callback = slot.callbacks.on_goal_response;
    auto* const callback_state = slot.callbacks.state;
    if (IsOk(status)) {
      slot.state = GoalState::kActive;
      ++stats_.goals_accepted;
      callback(callback_state, handle, status, context);
      return;
    }

    Release(slot);
    ++stats_.goals_rejected;
    callback(callback_state, handle, status, context);
  }

  void HandleCancel(Slot& slot, const ExecutionContext& context) noexcept {
    if (slot.state != GoalState::kActive || !slot.cancel_enqueued) {
      return;
    }
    slot.cancel_enqueued = false;
    const auto status =
        cancel_handler_(handler_state_, slot.handle, context);
    if (IsOk(status)) {
      slot.state = GoalState::kCancelRequested;
    }
    slot.callbacks.on_cancel_response(slot.callbacks.state, slot.handle, status,
                                      context);
  }

  Slot* Find(ActionGoalHandle handle) noexcept {
    if (!handle) {
      return nullptr;
    }
    for (auto& slot : slots_) {
      if (slot.state != GoalState::kFree && slot.handle == handle) {
        return &slot;
      }
    }
    return nullptr;
  }

  void Release(Slot& slot) noexcept {
    slot.state = GoalState::kFree;
    slot.callbacks = {};
    slot.handle = {};
    slot.deadline_ns = 0;
    slot.cancel_enqueued = false;
    --active_goals_;
  }

  std::string_view name_;
  Executor& executor_;
  GoalHandler goal_handler_;
  CancelHandler cancel_handler_;
  void* handler_state_;
  std::array<Slot, MaxGoals> slots_{};
  std::size_t active_goals_{};
  std::uint32_t next_goal_id_{};
  ActionStats stats_{};
};

}  // namespace xrobot::runtime
