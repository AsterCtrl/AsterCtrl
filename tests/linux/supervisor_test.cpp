#include "aster/platform/linux/supervisor.hpp"

#include <array>
#include <cassert>
#include <cstddef>

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
}
