#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "allocation_tracker.hpp"
#include "xrobot/runtime/action.hpp"
#include "xrobot/runtime/cooperative_executor.hpp"
#include "xrobot/transport/can/action_bridge.hpp"

namespace test {

struct MoveGoal {
  std::uint16_t distance{};
};
struct MoveFeedback {
  std::uint8_t progress{};
};
struct MoveResult {
  std::uint16_t travelled{};
};
struct Move {};

}  // namespace test

namespace xrobot::runtime {

template <>
struct TypeSupport<test::MoveGoal> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.action.Move.Goal", SchemaHash{{std::byte{0x51}}}, 2};
  }
  static Status Encode(const test::MoveGoal& value, std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 2) {
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(value.distance & 0xffU);
    output[1] = static_cast<std::byte>(value.distance >> 8U);
    written = 2;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::MoveGoal& value) noexcept {
    if (input.size() != 2) {
      return Status::kInvalidArgument;
    }
    value.distance = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(input[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(input[1]) << 8U));
    return Status::kOk;
  }
};

template <>
struct TypeSupport<test::MoveFeedback> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.action.Move.Feedback", SchemaHash{{std::byte{0x52}}}, 1};
  }
  static Status Encode(const test::MoveFeedback& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.empty()) {
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
    return {"test.action.Move.Result", SchemaHash{{std::byte{0x53}}}, 2};
  }
  static Status Encode(const test::MoveResult& value,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 2) {
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(value.travelled & 0xffU);
    output[1] = static_cast<std::byte>(value.travelled >> 8U);
    written = 2;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::MoveResult& value) noexcept {
    if (input.size() != 2) {
      return Status::kInvalidArgument;
    }
    value.travelled = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(input[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(input[1]) << 8U));
    return Status::kOk;
  }
};

template <>
struct ActionTypeSupport<test::Move> {
  using Goal = test::MoveGoal;
  using Feedback = test::MoveFeedback;
  using Result = test::MoveResult;
  static constexpr ActionDescriptor descriptor() noexcept {
    return {"test.action.Move", SchemaHash{{std::byte{0x54}}}};
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
using namespace xrobot::transport::can;

struct LocalState {
  ActionGoalHandle handle;
  std::uint32_t cancels{};
};

Status AcceptGoal(void* state, const test::MoveGoal&, ActionGoalHandle handle,
                  const ExecutionContext&) noexcept {
  static_cast<LocalState*>(state)->handle = handle;
  return Status::kOk;
}

Status AcceptCancel(void* state, ActionGoalHandle,
                    const ExecutionContext&) noexcept {
  ++static_cast<LocalState*>(state)->cancels;
  return Status::kOk;
}

struct ClientState {
  Status goal_status{Status::kInternal};
  std::uint8_t progress{};
  Status result_status{Status::kInternal};
  std::uint16_t travelled{};
  Status cancel_status{Status::kInternal};
};

void GoalResponse(void* state, ActionGoalHandle, Status status,
                  const ExecutionContext&) noexcept {
  static_cast<ClientState*>(state)->goal_status = status;
}
void Feedback(void* state, ActionGoalHandle,
              const test::MoveFeedback& feedback,
              const ExecutionContext&) noexcept {
  static_cast<ClientState*>(state)->progress = feedback.progress;
}
void Result(void* state, ActionGoalHandle, Status status,
            const test::MoveResult& result,
            const ExecutionContext&) noexcept {
  auto& client = *static_cast<ClientState*>(state);
  client.result_status = status;
  client.travelled = result.travelled;
}
void CancelResponse(void* state, ActionGoalHandle, Status status,
                    const ExecutionContext&) noexcept {
  static_cast<ClientState*>(state)->cancel_status = status;
}

ActionCallbacks<test::Move> Callbacks(ClientState& state) {
  return {GoalResponse, Feedback, Result, CancelResponse, &state};
}

struct Clock {
  std::uint64_t now_ns{1'000};
};
std::uint64_t ReadClock(void* state) noexcept {
  return static_cast<Clock*>(state)->now_ns;
}

struct ActionBus {
  CanActionClient<test::Move>* client{};
  CanActionServer<test::Move>* server{};
  const ExecutionContext* client_context{};
  const ExecutionContext* server_context{};
  Clock* clock{};
  std::array<CanFrame, 32> client_to_server{};
  std::array<CanFrame, 32> server_to_client{};
  std::size_t client_to_server_size{};
  std::size_t server_to_client_size{};
  std::size_t dropped_client_frame{static_cast<std::size_t>(-1)};
  std::size_t client_frames_seen{};
  bool drop_all_client_frames{};

  Status QueueClientFrame(const CanFrame& frame) noexcept {
    if (client_to_server_size == client_to_server.size()) {
      return Status::kCapacityExceeded;
    }
    client_to_server[client_to_server_size++] = frame;
    return Status::kOk;
  }

  Status QueueServerFrame(const CanFrame& frame) noexcept {
    if (server_to_client_size == server_to_client.size()) {
      return Status::kCapacityExceeded;
    }
    server_to_client[server_to_client_size++] = frame;
    return Status::kOk;
  }

  void Pump() noexcept {
    while (client_to_server_size != 0 || server_to_client_size != 0) {
      const auto client_count = client_to_server_size;
      client_to_server_size = 0;
      for (std::size_t index = 0; index < client_count; ++index) {
        const auto seen = client_frames_seen++;
        if (!drop_all_client_frames && seen != dropped_client_frame) {
          server->Accept(client_to_server[index], clock->now_ns,
                         *server_context);
        }
      }
      const auto server_count = server_to_client_size;
      server_to_client_size = 0;
      for (std::size_t index = 0; index < server_count; ++index) {
        client->Accept(server_to_client[index], clock->now_ns,
                       *client_context);
      }
    }
  }
};

Status ClientToServer(void* state, const CanFrame& frame,
                      const ExecutionContext&) noexcept {
  return static_cast<ActionBus*>(state)->QueueClientFrame(frame);
}
Status ServerToClient(void* state, const CanFrame& frame,
                      const ExecutionContext&) noexcept {
  return static_cast<ActionBus*>(state)->QueueServerFrame(frame);
}

void ActionGoalFeedbackResultAndCancelCrossCan() {
  CooperativeExecutor<4> server_executor("action", 4);
  LocalState local_state;
  StaticAction<test::Move, 1> local_action(
      "move", server_executor, AcceptGoal, AcceptCancel, &local_state);
  Clock clock;
  const ExecutionContext client_context("client", ExecutionKind::kThread, 4);
  const ExecutionContext server_context("server", ExecutionKind::kThread, 4);
  ActionBus bus;
  CanActionClient<test::Move> remote_client(
      19, CanPriority::kBackground, {ClientToServer, &bus},
      {ReadClock, &clock});
  CanActionServer<test::Move> remote_server(
      19, CanPriority::kBackground, local_action.client(),
      {ServerToClient, &bus}, {ReadClock, &clock});
  bus = {&remote_client, &remote_server, &client_context, &server_context,
         &clock};
  ClientState client_state;
  ActionGoalHandle remote_handle;

  assert(server_executor.Initialize() == Status::kOk);
  assert(server_executor.Start() == Status::kOk);
  assert(remote_client.client().SendGoal({50}, Callbacks(client_state), 0,
                                         client_context, remote_handle) ==
         Status::kOk);
  assert(client_state.goal_status == Status::kInternal);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(client_state.goal_status == Status::kOk);
  assert(local_state.handle);

  assert(local_action.server().PublishFeedback(local_state.handle, {60},
                                               server_context) == Status::kOk);
  bus.Pump();
  assert(client_state.progress == 60);
  assert(local_action.server().Finish(local_state.handle, Status::kOk, {50},
                                      server_context) == Status::kOk);
  bus.Pump();
  assert(client_state.result_status == Status::kOk);
  assert(client_state.travelled == 50);

  client_state = {};
  assert(remote_client.client().SendGoal({20}, Callbacks(client_state), 0,
                                         client_context, remote_handle) ==
         Status::kOk);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(remote_client.client().Cancel(remote_handle, client_context) ==
         Status::kOk);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(client_state.cancel_status == Status::kOk);
  assert(local_state.cancels == 1);
  assert(local_action.server().Finish(local_state.handle, Status::kCancelled,
                                      {}, server_context) == Status::kOk);
  bus.Pump();
  assert(client_state.result_status == Status::kCancelled);
}

void ActionRetriesAnIncompleteGoalAfterFrameLoss() {
  CooperativeExecutor<4> server_executor("action", 4);
  LocalState local_state;
  StaticAction<test::Move, 1> local_action(
      "move", server_executor, AcceptGoal, AcceptCancel, &local_state);
  Clock clock;
  const ExecutionContext client_context("client", ExecutionKind::kThread, 4);
  const ExecutionContext server_context("server", ExecutionKind::kThread, 4);
  ActionBus bus;
  CanActionClient<test::Move> remote_client(
      19, CanPriority::kBackground, {ClientToServer, &bus},
      {ReadClock, &clock}, 100, 2);
  CanActionServer<test::Move> remote_server(
      19, CanPriority::kBackground, local_action.client(),
      {ServerToClient, &bus}, {ReadClock, &clock}, 100, 2);
  bus.client = &remote_client;
  bus.server = &remote_server;
  bus.client_context = &client_context;
  bus.server_context = &server_context;
  bus.clock = &clock;
  bus.dropped_client_frame = 1;
  ClientState client_state;
  ActionGoalHandle remote_handle;

  assert(server_executor.Initialize() == Status::kOk);
  assert(server_executor.Start() == Status::kOk);
  assert(remote_client.client().SendGoal({30}, Callbacks(client_state), 0,
                                         client_context, remote_handle) ==
         Status::kOk);
  bus.Pump();
  assert(server_executor.pending() == 0);
  clock.now_ns += 100;
  assert(remote_client.Poll(clock.now_ns, client_context) == Status::kOk);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(client_state.goal_status == Status::kOk);
  assert(local_action.server().Finish(local_state.handle, Status::kOk, {30},
                                      server_context) == Status::kOk);
  bus.Pump();
  assert(client_state.travelled == 30);
}

void ActionTimeoutReleasesTheClientForANewGoal() {
  CooperativeExecutor<4> server_executor("action", 4);
  LocalState local_state;
  StaticAction<test::Move, 1> local_action(
      "move", server_executor, AcceptGoal, AcceptCancel, &local_state);
  Clock clock;
  const ExecutionContext client_context("client", ExecutionKind::kThread, 4);
  const ExecutionContext server_context("server", ExecutionKind::kThread, 4);
  ActionBus bus;
  CanActionClient<test::Move> remote_client(
      19, CanPriority::kBackground, {ClientToServer, &bus},
      {ReadClock, &clock}, 100, 1);
  CanActionServer<test::Move> remote_server(
      19, CanPriority::kBackground, local_action.client(),
      {ServerToClient, &bus}, {ReadClock, &clock}, 100, 1);
  bus.client = &remote_client;
  bus.server = &remote_server;
  bus.client_context = &client_context;
  bus.server_context = &server_context;
  bus.clock = &clock;
  bus.drop_all_client_frames = true;
  ClientState client_state;
  ActionGoalHandle handle;

  assert(server_executor.Initialize() == Status::kOk);
  assert(server_executor.Start() == Status::kOk);
  assert(remote_client.client().SendGoal({10}, Callbacks(client_state), 0,
                                         client_context, handle) == Status::kOk);
  bus.Pump();
  clock.now_ns += 100;
  assert(remote_client.Poll(clock.now_ns, client_context) == Status::kOk);
  bus.Pump();
  clock.now_ns += 100;
  assert(remote_client.Poll(clock.now_ns, client_context) == Status::kTimeout);
  assert(client_state.goal_status == Status::kTimeout);
  assert(remote_client.stats().timeouts == 1);

  bus.drop_all_client_frames = false;
  client_state = {};
  assert(remote_client.client().SendGoal({12}, Callbacks(client_state), 0,
                                         client_context, handle) == Status::kOk);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(client_state.goal_status == Status::kOk);
}

}  // namespace

int main() {
  const auto allocations = xrobot_test::AllocationCount();
  ActionGoalFeedbackResultAndCancelCrossCan();
  ActionRetriesAnIncompleteGoalAfterFrameLoss();
  ActionTimeoutReleasesTheClientForANewGoal();
  assert(xrobot_test::AllocationCount() == allocations);
  return 0;
}
