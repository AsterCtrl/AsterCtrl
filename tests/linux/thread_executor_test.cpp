#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

#include "aster/platform/linux/runtime_services.hpp"

namespace {

struct Observation {
  std::mutex mutex;
  std::condition_variable ready;
  std::array<int, 2> order{};
  std::size_t count{};
};

struct TaskState {
  Observation* observation{};
  int value{};
};

void Record(void* state, const aster::ExecutionContext& context) noexcept {
  auto& task = *static_cast<TaskState*>(state);
  assert(context.kind() == aster::ExecutionKind::kThread);
  assert(context.executor_name() == "worker");
  {
    const std::lock_guard lock(task.observation->mutex);
    task.observation->order[task.observation->count++] = task.value;
  }
  task.observation->ready.notify_one();
}

}  // namespace

int main() {
  aster::platform::linux::SteadyClock clock;
  aster::platform::linux::ThreadExecutor<4> executor("worker", clock);
  assert(executor.Prepare() == aster::Status::kOk);

  Observation observation;
  TaskState delayed{&observation, 1};
  TaskState immediate{&observation, 2};
  const aster::ExecutionContext caller("main", aster::ExecutionKind::kThread, clock.NowNs());
  assert(executor.TryPostAt(clock.NowNs() + 30'000'000, {Record, &delayed}, caller) ==
         aster::Status::kOk);
  assert(executor.TryPost({Record, &immediate}, caller) == aster::Status::kOk);

  {
    std::unique_lock lock(observation.mutex);
    assert(!observation.ready.wait_for(lock, std::chrono::milliseconds(20),
                                       [&] { return observation.count != 0; }));
  }
  assert(executor.Activate() == aster::Status::kOk);

  {
    std::unique_lock lock(observation.mutex);
    assert(observation.ready.wait_for(lock, std::chrono::seconds(2), [&] {
      return observation.count == observation.order.size();
    }));
  }
  assert(observation.order[0] == 2);
  assert(observation.order[1] == 1);

  const aster::ExecutionContext interrupt("irq", aster::ExecutionKind::kInterrupt, clock.NowNs());
  assert(executor.TryPost({Record, &immediate}, interrupt) == aster::Status::kInvalidArgument);
  executor.Shutdown();
  assert(executor.state() == aster::platform::linux::ThreadExecutorState::kStopped);

  aster::platform::linux::ThreadExecutor<1> cancelled("cancelled", clock);
  Observation cancelled_observation;
  TaskState cancelled_task{&cancelled_observation, 3};
  assert(cancelled.Prepare() == aster::Status::kOk);
  assert(cancelled.TryPost({Record, &cancelled_task}, caller) == aster::Status::kOk);
  cancelled.Shutdown();
  assert(cancelled_observation.count == 0);
  assert(cancelled.state() == aster::platform::linux::ThreadExecutorState::kStopped);

  aster::platform::linux::SystemAllocator allocator;
  void* memory = allocator.Allocate(64, 64);
  assert(memory != nullptr);
  allocator.Deallocate(memory, 64, 64);
}
