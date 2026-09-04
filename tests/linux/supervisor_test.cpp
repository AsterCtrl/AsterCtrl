#include "aster/platform/linux/supervisor.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

#include "aster/channel.hpp"
#include "aster/platform/linux/runtime_services.hpp"
#include "test_types.hpp"

namespace {

class Source final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"source", "test.Source", "test", {1, 0, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    core_ = core;
    return publisher_.Bind(core.channel(), "samples");
  }

  aster::Status Start() noexcept override {
    return publisher_.Publish({42}, core_.clock().NowNs(),
                              {"source", aster::ExecutionKind::kThread, core_.clock().NowNs()});
  }

  void Shutdown() noexcept override {}

 private:
  aster::CoreRef core_;
  aster::Publisher<test::Sample> publisher_;
};

class Sink final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"sink", "test.Sink", "test", {1, 0, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return subscriber_.Bind(core.channel(), "samples", Receive, this);
  }

  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}

  [[nodiscard]] std::uint32_t value() const noexcept { return value_; }

 private:
  static aster::Status Receive(void* state, const test::Sample& message, const aster::MessageInfo&,
                               const aster::ExecutionContext&) noexcept {
    static_cast<Sink*>(state)->value_ = message.value;
    return aster::Status::kOk;
  }

  aster::Subscriber<test::Sample> subscriber_;
  std::uint32_t value_{};
};

aster::transport::DeploymentId Id(std::byte value) {
  aster::transport::DeploymentId id;
  id.bytes.fill(value);
  return id;
}

aster::Status Count(void* state, const aster::platform::linux::GraphModuleView&) noexcept {
  ++*static_cast<std::size_t*>(state);
  return aster::Status::kOk;
}

struct WorkState {
  std::mutex mutex;
  std::condition_variable ready;
  std::size_t count{};
};

void CountWork(void* state, const aster::ExecutionContext&) noexcept {
  auto& work = *static_cast<WorkState*>(state);
  {
    const std::lock_guard lock(work.mutex);
    ++work.count;
  }
  work.ready.notify_all();
}

class QueuedModule final : public aster::Module {
 public:
  QueuedModule(WorkState& work, bool fail_start) noexcept : work_(work), fail_start_(fail_start) {}

  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"queued", "test.Queued", "test", {1, 0, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    executor_ = core.executor();
    return executor_.TryPost({CountWork, &work_}, {"initialize", aster::ExecutionKind::kThread, 0});
  }

  aster::Status Start() noexcept override {
    {
      const std::lock_guard lock(work_.mutex);
      count_seen_in_start_ = work_.count;
    }
    const auto status =
        executor_.TryPost({CountWork, &work_}, {"start", aster::ExecutionKind::kThread, 0});
    return fail_start_ || !aster::IsOk(status) ? aster::Status::kUnavailable : aster::Status::kOk;
  }

  void Shutdown() noexcept override { shutdown_called_.store(true); }

  [[nodiscard]] std::size_t count_seen_in_start() const noexcept { return count_seen_in_start_; }
  [[nodiscard]] bool shutdown_called() const noexcept { return shutdown_called_.load(); }

 private:
  WorkState& work_;
  bool fail_start_{};
  aster::ExecutorRef executor_;
  std::size_t count_seen_in_start_{};
  std::atomic<bool> shutdown_called_{};
};

struct BlockingWorkState {
  std::mutex mutex;
  std::condition_variable changed;
  bool running{};
  bool release{};
  bool module_shutdown{};
};

void BlockWork(void* state, const aster::ExecutionContext&) noexcept {
  auto& work = *static_cast<BlockingWorkState*>(state);
  std::unique_lock lock(work.mutex);
  work.running = true;
  work.changed.notify_all();
  work.changed.wait(lock, [&] { return work.release; });
}

class BlockingModule final : public aster::Module {
 public:
  explicit BlockingModule(BlockingWorkState& work) noexcept : work_(work) {}

  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"blocking", "test.Blocking", "test", {1, 0, 0}};
  }

  aster::Status Initialize(aster::CoreRef core) noexcept override {
    executor_ = core.executor();
    return aster::Status::kOk;
  }

  aster::Status Start() noexcept override {
    return executor_.TryPost({BlockWork, &work_}, {"start", aster::ExecutionKind::kThread, 0});
  }

  void Shutdown() noexcept override {
    const std::lock_guard lock(work_.mutex);
    work_.module_shutdown = true;
    work_.changed.notify_all();
  }

 private:
  BlockingWorkState& work_;
  aster::ExecutorRef executor_;
};

template <std::size_t Capacity>
aster::platform::linux::ExecutorLifecycle Lifecycle(
    aster::platform::linux::ThreadExecutor<Capacity>& executor) noexcept {
  return {
      [](void* state) noexcept {
        return static_cast<aster::platform::linux::ThreadExecutor<Capacity>*>(state)->Prepare();
      },
      [](void* state) noexcept {
        return static_cast<aster::platform::linux::ThreadExecutor<Capacity>*>(state)->Activate();
      },
      [](void* state) noexcept {
        static_cast<aster::platform::linux::ThreadExecutor<Capacity>*>(state)->Shutdown();
      },
      &executor,
  };
}

}  // namespace

int main() {
  aster::platform::linux::SteadyClock clock;
  aster::LocalChannel<1, 1, 8> channel;
  auto handles = aster::CoreHandles{};
  handles.channel = aster::ChannelRef(channel);
  handles.clock = aster::ClockRef(clock);
  const aster::CoreRef core(handles);
  Source source;
  Sink sink;
  const auto deployment = Id(std::byte{0x21});
  aster::platform::linux::Supervisor supervisor(core, deployment);
  assert(supervisor.AddModule(source) == aster::Status::kOk);
  assert(supervisor.AddModule(sink) == aster::Status::kOk);
  assert(supervisor.AddRegistry(channel) == aster::Status::kOk);
  assert(supervisor.Start(Id(std::byte{0x22})) == aster::Status::kVersionMismatch);

  std::size_t graph_modules{};
  assert(supervisor.VisitGraph(Count, &graph_modules) == aster::Status::kOk);
  assert(graph_modules == 2);
  assert(supervisor.Start(deployment) == aster::Status::kOk);
  assert(sink.value() == 42);
  assert(supervisor.state() == aster::platform::linux::SupervisorState::kRunning);
  assert(supervisor.failure() == nullptr);
  supervisor.Shutdown();
  assert(supervisor.state() == aster::platform::linux::SupervisorState::kStopped);

  {
    aster::platform::linux::ThreadExecutor<4> executor("gated", clock);
    auto executor_handles = handles;
    executor_handles.executor = aster::ExecutorRef(executor);
    WorkState work;
    QueuedModule queued(work, false);
    aster::platform::linux::Supervisor gated(aster::CoreRef(executor_handles), deployment,
                                             Lifecycle(executor));
    assert(gated.AddModule(queued) == aster::Status::kOk);
    assert(gated.Start(deployment) == aster::Status::kOk);
    assert(queued.count_seen_in_start() == 0);
    {
      std::unique_lock lock(work.mutex);
      assert(work.ready.wait_for(lock, std::chrono::seconds(2), [&] { return work.count == 2; }));
    }
    gated.Shutdown();
    assert(queued.shutdown_called());
    assert(executor.state() == aster::platform::linux::ThreadExecutorState::kStopped);
  }

  {
    aster::platform::linux::ThreadExecutor<4> executor("rollback", clock);
    auto executor_handles = handles;
    executor_handles.executor = aster::ExecutorRef(executor);
    WorkState work;
    QueuedModule failing(work, true);
    aster::platform::linux::Supervisor rollback(aster::CoreRef(executor_handles), deployment,
                                                Lifecycle(executor));
    assert(rollback.AddModule(failing) == aster::Status::kOk);
    assert(rollback.Start(deployment) == aster::Status::kUnavailable);
    assert(work.count == 0);
    assert(failing.shutdown_called());
    assert(executor.state() == aster::platform::linux::ThreadExecutorState::kStopped);
  }

  {
    aster::platform::linux::ThreadExecutor<1> executor("quiesce", clock);
    auto executor_handles = handles;
    executor_handles.executor = aster::ExecutorRef(executor);
    BlockingWorkState work;
    BlockingModule blocking(work);
    aster::platform::linux::Supervisor quiesce(aster::CoreRef(executor_handles), deployment,
                                               Lifecycle(executor));
    assert(quiesce.AddModule(blocking) == aster::Status::kOk);
    assert(quiesce.Start(deployment) == aster::Status::kOk);
    {
      std::unique_lock lock(work.mutex);
      assert(work.changed.wait_for(lock, std::chrono::seconds(2), [&] { return work.running; }));
    }

    std::thread shutdown([&] { quiesce.Shutdown(); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (executor.state() != aster::platform::linux::ThreadExecutorState::kQuiescing &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    assert(executor.state() == aster::platform::linux::ThreadExecutorState::kQuiescing);
    {
      const std::lock_guard lock(work.mutex);
      assert(!work.module_shutdown);
      work.release = true;
    }
    work.changed.notify_all();
    shutdown.join();
    assert(work.module_shutdown);
    assert(executor.state() == aster::platform::linux::ThreadExecutorState::kStopped);
  }
}
