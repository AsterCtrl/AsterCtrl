#include "aster_runtime/platform/linux/channel_manager.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "aster_runtime/platform/linux/runtime_services.hpp"
#include "test_types.hpp"

namespace {
struct Received {
  std::mutex mutex;
  std::condition_variable ready;
  std::uint32_t value{};
  std::string executor;
};
aster::Status Receive(void* state, const test::Sample& message, const aster::MessageInfo&,
                      const aster::ExecutionContext& context) noexcept {
  auto& received = *static_cast<Received*>(state);
  const std::lock_guard lock(received.mutex);
  received.value = message.value;
  received.executor = context.executor_name();
  received.ready.notify_one();
  return aster::Status::kOk;
}
}  // namespace

int main() {
  using namespace aster::platform::linux;
  using aster::Status;
  SteadyClock clock;
  ThreadExecutor<0> first_executor("first", clock, 1, 1);
  ThreadExecutor<0> second_executor("second", clock, 1, 1);
  assert(first_executor.Prepare() == Status::kOk);
  assert(second_executor.Prepare() == Status::kOk);
  ChannelManager manager;
  ModuleConfig first;
  first.name_space = "/left";
  first.remap.emplace("/left/state", "/shared");
  ModuleConfig second;
  second.name_space = "/right";
  second.remap.emplace("/right/state", "/shared");
  auto a = manager.CreateEndpoint(first, aster::ExecutorRef(first_executor));
  auto b = manager.CreateEndpoint(second, aster::ExecutorRef(second_executor));
  aster::Publisher<test::Sample> publisher;
  aster::Subscriber<test::Sample> first_subscriber;
  aster::Subscriber<test::Sample> second_subscriber;
  Received first_received;
  Received second_received;
  assert(publisher.Bind(aster::ChannelRef(*a), "state") == Status::kOk);
  assert(first_subscriber.Bind(aster::ChannelRef(*a), "state", Receive, &first_received) ==
         Status::kOk);
  assert(second_subscriber.Bind(aster::ChannelRef(*b), "state", Receive, &second_received) ==
         Status::kOk);
  auto wrong = aster::TypeSupport<test::Sample>::descriptor();
  wrong.max_serialized_size += 1;
  assert(b->RegisterPublisher({"state", wrong}) == Status::kTypeMismatch);
  assert(manager.Seal() == Status::kOk);
  test::Sample sample{42};
  assert(publisher.Publish(sample) == Status::kOk);
  sample.value = 99;  // Queued delivery owns the encoded payload.
  assert(publisher.Publish(sample) == Status::kCapacityExceeded);
  assert(a->RegisterPublisher({"late", aster::TypeSupport<test::Sample>::descriptor()}) ==
         Status::kInvalidState);
  assert(first_executor.Activate() == Status::kOk);
  assert(second_executor.Activate() == Status::kOk);
  for (auto* received : {&first_received, &second_received}) {
    std::unique_lock lock(received->mutex);
    assert(received->ready.wait_for(lock, std::chrono::seconds(2),
                                    [&] { return received->value != 0; }));
    assert(received->value == 42);
  }
  assert(first_received.executor == "first" && second_received.executor == "second");
  manager.Close();
  first_executor.Shutdown();
  second_executor.Shutdown();
  manager.ClearPending();
  assert(publisher.Publish(sample) == Status::kInvalidState);
}
