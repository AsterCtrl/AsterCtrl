#include "aster/platform/linux/node_runtime.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include "aster/channel.hpp"
#include "aster/module.hpp"

namespace {

constexpr aster::SchemaHash Schema() {
  aster::SchemaHash hash{};
  hash.bytes[0] = std::byte{0x72};
  return hash;
}

constexpr aster::ChannelDescriptor Descriptor() {
  return {"state", {"example.State", Schema(), 1}};
}

class Source final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"source", "example.Source", "tests", {0, 1, 0}};
  }
  aster::Status Initialize(aster::CoreRef core) noexcept override {
    channel_ = core.channel();
    return channel_.RegisterPublisher(Descriptor());
  }
  aster::Status Start() noexcept override {
    const std::array message{std::byte{42}};
    return channel_.Publish(Descriptor(), message, 10, {"io", aster::ExecutionKind::kThread, 10});
  }
  void Shutdown() noexcept override {}

 private:
  aster::ChannelRef channel_;
};

class Sink final : public aster::Module {
 public:
  [[nodiscard]] aster::ModuleInfo Info() const noexcept override {
    return {"sink", "example.Sink", "tests", {0, 1, 0}};
  }
  aster::Status Initialize(aster::CoreRef core) noexcept override {
    return core.channel().RegisterSubscriber(Descriptor(), Receive, this);
  }
  aster::Status Start() noexcept override { return aster::Status::kOk; }
  void Shutdown() noexcept override {}

  static aster::Status Receive(void* state, std::span<const std::byte> message,
                               const aster::MessageInfo&, const aster::ExecutionContext&) noexcept {
    static_cast<Sink*>(state)->value = message.empty() ? std::byte{} : message.front();
    return aster::Status::kOk;
  }

  std::byte value{};
};

struct Composition {
  Source source;
  Sink sink;
  std::array<aster::ModuleSlot, 2> modules{
      aster::ModuleSlot{&source, {}, "source"},
      aster::ModuleSlot{&sink, {}, "sink"},
  };
  std::array<aster::RegistrySlot, 0> registries{};

  explicit Composition(aster::CoreRef core) noexcept {
    modules[0].core = core;
    modules[1].core = core;
  }

  std::span<aster::ModuleSlot> Modules() noexcept { return modules; }
  std::span<aster::RegistrySlot> Registries() noexcept { return registries; }
};

void RunsAStaticCompositionThroughTheLinuxSupervisor() {
  aster::transport::DeploymentId deployment;
  deployment.bytes[0] = std::byte{0x44};
  aster::platform::linux::StaticNodeRuntime<Composition, 1, 1, 1, 1, 1, 1, 2> node(deployment,
                                                                                   "io");

  assert(node.Start() == aster::Status::kOk);
  assert(node.composition().sink.value == std::byte{42});
  assert(node.state() == aster::platform::linux::SupervisorState::kRunning);
  assert(node.Start() == aster::Status::kInvalidState);
  node.Shutdown();
  assert(node.state() == aster::platform::linux::SupervisorState::kStopped);
}

}  // namespace

int main() {
  RunsAStaticCompositionThroughTheLinuxSupervisor();
  return 0;
}
