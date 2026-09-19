#include <dlfcn.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "aster_runtime/platform/linux/runtime.hpp"
#include "test_types.hpp"

class Sink final : public aster::ModuleBase {
 public:
  aster::ModuleInfo Info() const noexcept override {
    return {"sink", "test.Sink", "test", {1, 0, 0}};
  }
  aster::Status Initialize(aster::CoreRef core) override {
    return subscriber_.Bind(core.channel(), "state", Receive, this);
  }
  aster::Status Start() override { return aster::Status::kOk; }
  void Shutdown() noexcept override { stopped = true; }
  bool stopped{};
  std::mutex mutex;
  std::condition_variable ready;
  std::vector<std::uint32_t> values;

 private:
  static aster::Status Receive(void* state, const test::Sample& sample, const aster::MessageInfo&,
                               const aster::ExecutionContext& context) noexcept {
    assert(context.executor_name() == "sink");
    auto& self = *static_cast<Sink*>(state);
    const std::lock_guard lock(self.mutex);
    self.values.push_back(sample.value);
    self.ready.notify_one();
    return aster::Status::kOk;
  }
  aster::Subscriber<test::Sample> subscriber_;
};

class Failing final : public aster::ModuleBase {
 public:
  explicit Failing(bool during_initialize) : during_initialize_(during_initialize) {}
  aster::ModuleInfo Info() const noexcept override {
    return {"failure", "test.Failing", "test", {1, 0, 0}};
  }
  aster::Status Initialize(aster::CoreRef) override {
    return during_initialize_ ? aster::Status::kInternal : aster::Status::kOk;
  }
  aster::Status Start() override { return aster::Status::kInternal; }
  void Shutdown() noexcept override { ++shutdown_count; }
  unsigned shutdown_count{};

 private:
  bool during_initialize_;
};

int main() {
  using namespace aster::platform::linux;
  using aster::Status;
  for (int second_gain : {3, 7}) {
    Sink sink;
    NodeRuntime runtime;
    assert(runtime.RegisterModule("sink", sink) == Status::kOk);
    const std::string yaml = R"(
api_version: aster.dev/v1alpha3
aster:
  executors:
    - {name: first, type: serial, queue_capacity: 8}
    - {name: second, type: thread_pool, threads: 2, queue_capacity: 8}
    - {name: sink, type: serial, queue_capacity: 8}
  packages:
    - {name: demo, path: ')" +
                             std::string(ASTER_TEST_CPP_PACKAGE_PATH) + R"('}
  modules:
    - name: left
      type: test.ConfiguredSource
      package: demo
      executor: first
      namespace: /left
      remap: {/left/state: /robot/state}
      config: {gain: 2, expected_executor: first}
    - name: right
      type: test.ConfiguredSource
      package: demo
      executor: second
      namespace: /right
      remap: {/right/state: /robot/state}
      config: {gain: )" + std::to_string(second_gain) +
                             R"(, expected_executor: second}
    - {name: disabled, type: test.ConfiguredSource, package: demo, enabled: false}
    - {name: sink, type: test.Sink, executor: sink, namespace: /robot}
)";
    assert(runtime.Initialize(yaml) == Status::kOk);
    assert(runtime.Start() == Status::kOk);
    {
      std::unique_lock lock(sink.mutex);
      assert(sink.ready.wait_for(lock, std::chrono::seconds(2),
                                 [&] { return sink.values.size() == 2; }));
      assert(sink.values[0] == 2 && sink.values[1] == static_cast<std::uint32_t>(second_gain));
    }
    runtime.Shutdown();
    assert(sink.stopped);
  }
  for (bool during_initialize : {false, true}) {
    Sink sink;
    Failing failure(during_initialize);
    NodeRuntime runtime;
    assert(runtime.RegisterModule("sink", sink) == Status::kOk);
    assert(runtime.RegisterModule("failure", failure) == Status::kOk);
    const auto yaml = std::string(R"(
api_version: aster.dev/v1alpha3
aster:
  executors: [{name: sink, type: serial}]
  packages: [{name: demo, path: ')") +
                      ASTER_TEST_CPP_PACKAGE_PATH + R"('}]
  modules:
    - {name: source, type: test.ConfiguredSource, package: demo, config: {gain: 42, expected_executor: sink}}
    - {name: sink, type: test.Sink}
    - {name: failure, type: test.Failing}
)";
    if (during_initialize)
      assert(runtime.Initialize(yaml) == Status::kInternal);
    else {
      assert(runtime.Initialize(yaml) == Status::kOk);
      assert(runtime.Start() == Status::kInternal);
    }
    assert(runtime.state() == SupervisorState::kFailed);
    assert(sink.stopped && sink.values.empty() && failure.shutdown_count == 1);
    assert(runtime.diagnostic().find("failure") != std::string_view::npos);
    void* loaded = dlopen(ASTER_TEST_CPP_PACKAGE_PATH, RTLD_NOW | RTLD_NOLOAD);
    assert(loaded == nullptr);  // No callback or instance can retain the Package.
  }
}
