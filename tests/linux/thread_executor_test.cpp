#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <thread>

#include "aster_runtime/platform/linux/runtime_services.hpp"

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

struct ParallelObservation {
  std::mutex mutex;
  std::condition_variable ready;
  std::size_t entered{};
  bool release{};
};

void Rendezvous(void* state, const aster::ExecutionContext& context) noexcept {
  assert(context.executor_name() == "pool");
  auto& observation = *static_cast<ParallelObservation*>(state);
  std::unique_lock lock(observation.mutex);
  ++observation.entered;
  observation.ready.notify_all();
  observation.ready.wait(lock, [&] { return observation.release; });
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
  assert(executor.TryPostAt(clock.NowNs() + 30'000'000, aster::WorkItem::Bind<Record>(&delayed),
                            caller) == aster::Status::kOk);
  assert(executor.TryPost(aster::WorkItem::Bind<Record>(&immediate), caller) == aster::Status::kOk);

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
  assert(executor.TryPost(aster::WorkItem::Bind<Record>(&immediate), interrupt) ==
         aster::Status::kInvalidArgument);
  executor.Shutdown();
  assert(executor.state() == aster::platform::linux::ThreadExecutorState::kStopped);

  aster::platform::linux::ThreadExecutor<1> cancelled("cancelled", clock);
  Observation cancelled_observation;
  TaskState cancelled_task{&cancelled_observation, 3};
  assert(cancelled.Prepare() == aster::Status::kOk);
  assert(cancelled.TryPost(aster::WorkItem::Bind<Record>(&cancelled_task), caller) ==
         aster::Status::kOk);
  cancelled.Shutdown();
  assert(cancelled_observation.count == 0);
  assert(cancelled.state() == aster::platform::linux::ThreadExecutorState::kStopped);

  aster::platform::linux::SystemAllocator allocator;
  void* memory = allocator.Allocate(64, 64);
  assert(memory != nullptr);
  allocator.Deallocate(memory, 64, 64);

  aster::platform::linux::ThreadExecutor<0> pool("pool", clock, 2, 2);
  ParallelObservation parallel;
  assert(pool.Prepare() == aster::Status::kOk);
  const auto work = aster::WorkItem::Bind<Rendezvous>(&parallel);
  assert(pool.TryPost(work, caller) == aster::Status::kOk);
  assert(pool.TryPost(work, caller) == aster::Status::kOk);
  assert(pool.TryPost(work, caller) == aster::Status::kCapacityExceeded);
  assert(pool.Activate() == aster::Status::kOk);
  {
    std::unique_lock lock(parallel.mutex);
    assert(parallel.ready.wait_for(lock, std::chrono::seconds(2),
                                   [&] { return parallel.entered == 2; }));
    parallel.release = true;
  }
  parallel.ready.notify_all();
  pool.Shutdown();
  assert(pool.TryPost(work, caller) == aster::Status::kInvalidState);
  aster::platform::linux::ThreadExecutor<0> invalid("empty", clock, 0, 1);
  assert(invalid.Prepare() == aster::Status::kInvalidArgument);

  aster::platform::linux::ThreadExecutor<0> reserved("reserved", clock, 1, 1);
  assert(reserved.Prepare() == aster::Status::kOk);
  std::uint64_t ticket{};
  assert(reserved.Reserve(work, ticket) == aster::Status::kOk);
  assert(reserved.TryPost(work, caller) == aster::Status::kCapacityExceeded);
  assert(reserved.CancelReserved(ticket) == aster::Status::kOk);
  assert(reserved.ScheduleReserved(ticket, clock.NowNs()) == aster::Status::kNotFound);
  assert(reserved.TryPost(work, caller) == aster::Status::kOk);
  reserved.Shutdown();

  aster::platform::linux::ThreadExecutor<1> distant("worker", clock);
  Observation distant_observation;
  TaskState distant_task{&distant_observation, 4};
  assert(distant.Prepare() == aster::Status::kOk);
  assert(distant.TryPostAt(std::numeric_limits<std::uint64_t>::max(),
                           aster::WorkItem::Bind<Record>(&distant_task),
                           caller) == aster::Status::kOk);
  assert(distant.Activate() == aster::Status::kOk);
  {
    std::unique_lock lock(distant_observation.mutex);
    assert(!distant_observation.ready.wait_for(lock, std::chrono::milliseconds(30),
                                               [&] { return distant_observation.count != 0; }));
  }
  distant.Shutdown();  // A far-future timer remains cancellable, without chrono overflow.
}
