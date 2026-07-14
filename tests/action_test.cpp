#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "xrobot/runtime/action.hpp"
#include "xrobot/runtime/cooperative_executor.hpp"

namespace test {

struct MoveGoal {
  std::uint8_t distance{};
};

struct MoveFeedback {
  std::uint8_t progress{};
};

struct MoveResult {
  std::uint8_t travelled{};
};

struct Move {};

}  // namespace test

namespace xrobot::runtime {

template <>
struct TypeSupport<test::MoveGoal> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.action.Move.Goal", SchemaHash{{std::byte{0x01}}}, 1};
  }
  static Status Encode(const test::MoveGoal& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.empty()) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(value.distance);
    written = 1;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::MoveGoal& value) noexcept {
    if (input.size() != 1) {
      return Status::kInvalidArgument;
    }
    value.distance = static_cast<std::uint8_t>(input[0]);
    return Status::kOk;
  }
};

template <>
struct TypeSupport<test::MoveFeedback> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.action.Move.Feedback", SchemaHash{{std::byte{0x02}}}, 1};
  }
  static Status Encode(const test::MoveFeedback& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.empty()) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(value.progress);
    written = 1;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::MoveFeedback& value) noexcept {
    if (input.size() != 1) {
      return Status::kInvalidArgument;
    }
    value.progress = static_cast<std::uint8_t>(input[0]);
    return Status::kOk;
  }
};

template <>
struct TypeSupport<test::MoveResult> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.action.Move.Result", SchemaHash{{std::byte{0x03}}}, 1};
  }
  static Status Encode(const test::MoveResult& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.empty()) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(value.travelled);
    written = 1;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::MoveResult& value) noexcept {
    if (input.size() != 1) {
      return Status::kInvalidArgument;
    }
    value.travelled = static_cast<std::uint8_t>(input[0]);
    return Status::kOk;
  }
};

template <>
struct ActionTypeSupport<test::Move> {
  using Goal = test::MoveGoal;
  using Feedback = test::MoveFeedback;
  using Result = test::MoveResult;

  static constexpr ActionDescriptor descriptor() noexcept {
    return {"test.action.Move", SchemaHash{{std::byte{0xa1}}}};
  }
};

}  // namespace xrobot::runtime

namespace {

using xrobot::runtime::ActionCallbacks;
using xrobot::runtime::ActionGoalHandle;
using xrobot::runtime::CooperativeExecutor;
using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::StaticAction;
using xrobot::runtime::Status;

struct ServerState {
  ActionGoalHandle last_goal;
  std::uint8_t distance{};
  std::uint32_t cancel_count{};
};

struct ClientState {
  bool goal_response{};
  Status goal_status{Status::kInternal};
  std::uint8_t feedback{};
  bool result_received{};
  Status result_status{Status::kInternal};
  std::uint8_t travelled{};
  bool cancel_response{};
  Status cancel_status{Status::kInternal};
};

Status AcceptGoal(void* state, const test::MoveGoal& goal,
                  ActionGoalHandle handle,
                  const ExecutionContext&) noexcept {
  auto& server = *static_cast<ServerState*>(state);
  server.last_goal = handle;
  server.distance = goal.distance;
  return goal.distance == 0 ? Status::kInvalidArgument : Status::kOk;
}

Status AcceptCancel(void* state, ActionGoalHandle,
                    const ExecutionContext&) noexcept {
  ++static_cast<ServerState*>(state)->cancel_count;
  return Status::kOk;
}

void OnGoal(void* state, ActionGoalHandle, Status status,
            const ExecutionContext&) noexcept {
  auto& client = *static_cast<ClientState*>(state);
  client.goal_response = true;
  client.goal_status = status;
}

void OnFeedback(void* state, ActionGoalHandle,
                const test::MoveFeedback& feedback,
                const ExecutionContext&) noexcept {
  static_cast<ClientState*>(state)->feedback = feedback.progress;
}

void OnResult(void* state, ActionGoalHandle, Status status,
              const test::MoveResult& result,
              const ExecutionContext&) noexcept {
  auto& client = *static_cast<ClientState*>(state);
  client.result_received = true;
  client.result_status = status;
  client.travelled = result.travelled;
}

void OnCancel(void* state, ActionGoalHandle, Status status,
              const ExecutionContext&) noexcept {
  auto& client = *static_cast<ClientState*>(state);
  client.cancel_response = true;
  client.cancel_status = status;
}

ActionCallbacks<test::Move> Callbacks(ClientState& state) {
  return {OnGoal, OnFeedback, OnResult, OnCancel, &state};
}

void GoalFeedbackAndResultFollowTheActionStateMachine() {
  CooperativeExecutor<2> executor("action", 4);
  ServerState server_state;
  ClientState client_state;
  StaticAction<test::Move, 1> action("robot/move", executor, AcceptGoal,
                                    AcceptCancel, &server_state);
  const ExecutionContext caller("planner", ExecutionKind::kThread, 3);
  ActionGoalHandle handle;
  ActionGoalHandle rejected_by_capacity;

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(action.client().SendGoal({10}, Callbacks(client_state), 1'000,
                                  caller, handle) == Status::kOk);
  assert(action.client().SendGoal({20}, Callbacks(client_state), 1'000,
                                  caller, rejected_by_capacity) ==
         Status::kCapacityExceeded);
  assert(handle.id == 1);
  assert(!client_state.goal_response);

  assert(executor.RunOne() == Status::kOk);
  assert(client_state.goal_response);
  assert(client_state.goal_status == Status::kOk);
  assert(server_state.distance == 10);
  assert(action.server().PublishFeedback(handle, {60}, executor.context()) ==
         Status::kOk);
  assert(client_state.feedback == 60);
  assert(action.server().Finish(handle, Status::kOk, {10},
                                executor.context()) == Status::kOk);
  assert(client_state.result_received);
  assert(client_state.result_status == Status::kOk);
  assert(client_state.travelled == 10);
  assert(action.active_goals() == 0);
}

void CancellationIsRequestedOnTheServerExecutor() {
  CooperativeExecutor<2> executor("action", 4);
  ServerState server_state;
  ClientState client_state;
  StaticAction<test::Move, 1> action("robot/move", executor, AcceptGoal,
                                    AcceptCancel, &server_state);
  const ExecutionContext caller("planner", ExecutionKind::kThread, 3);
  ActionGoalHandle handle;

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(action.client().SendGoal({10}, Callbacks(client_state), 0, caller,
                                  handle) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(action.client().Cancel(handle, caller) == Status::kOk);
  assert(!client_state.cancel_response);
  assert(executor.RunOne() == Status::kOk);
  assert(client_state.cancel_response);
  assert(client_state.cancel_status == Status::kOk);
  assert(server_state.cancel_count == 1);
  assert(action.server().Finish(handle, Status::kCancelled, {},
                                executor.context()) == Status::kOk);
  assert(client_state.result_status == Status::kCancelled);
}

void DeadlineExpiresAnAcceptedGoal() {
  CooperativeExecutor<1> executor("action", 4);
  ServerState server_state;
  ClientState client_state;
  StaticAction<test::Move, 1> action("robot/move", executor, AcceptGoal,
                                    AcceptCancel, &server_state);
  const ExecutionContext caller("planner", ExecutionKind::kThread, 3);
  ActionGoalHandle handle;

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(action.client().SendGoal({10}, Callbacks(client_state), 100, caller,
                                  handle) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(action.ExpireDeadlines(99, executor.context()) == 0);
  assert(action.ExpireDeadlines(100, executor.context()) == 1);
  assert(client_state.result_received);
  assert(client_state.result_status == Status::kTimeout);
}

}  // namespace

int main() {
  static_assert(xrobot::runtime::ActionType<test::Move>);
  GoalFeedbackAndResultFollowTheActionStateMachine();
  CancellationIsRequestedOnTheServerExecutor();
  DeadlineExpiresAnAcceptedGoal();
  return 0;
}
