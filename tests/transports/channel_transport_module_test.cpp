#include "aster/transport/channel_transport_module.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/channel.hpp"
#include "aster/runtime.hpp"

namespace {

constexpr aster::SchemaHash Schema() {
  aster::SchemaHash hash{};
  hash.bytes[0] = std::byte{0x27};
  return hash;
}

constexpr aster::ChannelDescriptor Descriptor() {
  return {"command", {"example.Command", Schema(), 8}};
}

class ManualClock final : public aster::Clock {
 public:
  [[nodiscard]] aster::ClockDomain domain() const noexcept override {
    return aster::ClockDomain::kMonotonic;
  }
  [[nodiscard]] std::uint64_t NowNs() const noexcept override { return now_ns; }

  std::uint64_t now_ns{100};
};

class QueuedExecutor final : public aster::Executor {
 public:
  [[nodiscard]] std::string_view Name() const noexcept override { return "io"; }

  aster::Status TryPost(aster::WorkItem work,
                        const aster::ExecutionContext& caller) noexcept override {
    return TryPostAt(caller.timestamp_ns(), work, caller);
  }

  aster::Status TryPostAt(std::uint64_t timestamp_ns, aster::WorkItem work,
                          const aster::ExecutionContext&) noexcept override {
    if (!work || queued_) {
      return !work ? aster::Status::kInvalidArgument : aster::Status::kCapacityExceeded;
    }
    timestamp_ns_ = timestamp_ns;
    work_ = work;
    queued_ = true;
    return aster::Status::kOk;
  }

  void RunOne(ManualClock& clock) noexcept {
    assert(queued_);
    const auto work = work_;
    queued_ = false;
    clock.now_ns = timestamp_ns_;
    work.Run({"io", aster::ExecutionKind::kThread, clock.now_ns});
  }

 private:
  aster::WorkItem work_{};
  std::uint64_t timestamp_ns_{};
  bool queued_{};
};

class TestTransport final : public aster::transport::Transport {
 public:
  aster::Status Start(aster::transport::PacketReceiver receiver,
                      void* receiver_state) noexcept override {
    if (receiver == nullptr || started_) {
      return receiver == nullptr ? aster::Status::kInvalidArgument : aster::Status::kInvalidState;
    }
    receiver_ = receiver;
    receiver_state_ = receiver_state;
    started_ = true;
    return aster::Status::kOk;
  }

  aster::Status Send(const aster::transport::PacketView&,
                     const aster::ExecutionContext&) noexcept override {
    return started_ ? aster::Status::kOk : aster::Status::kInvalidState;
  }

  aster::Status Poll(const aster::ExecutionContext&) noexcept override {
    ++polls;
    return started_ ? aster::Status::kUnavailable : aster::Status::kInvalidState;
  }

  void Stop() noexcept override {
    started_ = false;
    receiver_ = nullptr;
    receiver_state_ = nullptr;
  }

  [[nodiscard]] aster::transport::TransportStats stats() const noexcept override { return {}; }

  aster::Status Inject(const aster::transport::PacketView& packet,
                       const aster::ExecutionContext& caller) noexcept {
    return receiver_ == nullptr ? aster::Status::kInvalidState
                                : receiver_(receiver_state_, packet, caller);
  }

  std::uint32_t polls{};

 private:
  aster::transport::PacketReceiver receiver_{};
  void* receiver_state_{};
  bool started_{};
};

class SinkModule final : public aster::Module {
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
                               const aster::MessageInfo& info,
                               const aster::ExecutionContext&) noexcept {
    auto& self = *static_cast<SinkModule*>(state);
    self.size = message.size();
    self.source_timestamp_ns = info.source_timestamp_ns;
    return aster::Status::kOk;
  }

  std::size_t size{};
  std::uint64_t source_timestamp_ns{};
};

void OwnsTransportLifecycleAndRoutesIngress() {
  ManualClock clock;
  QueuedExecutor executor;
  aster::LocalChannel<1, 1, 8> channel;
  TestTransport transport;
  aster::transport::ChannelTransportModule<0, 1> transport_module("usb0", transport, 10);
  SinkModule sink;
  auto handles = aster::CoreHandles{};
  handles.executor = aster::ExecutorRef(executor);
  handles.channel = aster::ChannelRef(channel);
  handles.clock = aster::ClockRef(clock);
  const aster::CoreRef core(handles);
  std::array modules{aster::ModuleSlot{&transport_module, core, "usb0"},
                     aster::ModuleSlot{&sink, core, "sink"}};
  std::array registries{aster::RegistrySlot{&channel}};
  aster::Runtime runtime(modules, registries);

  assert(transport_module.AddIngress(8, Descriptor()) == aster::Status::kOk);
  assert(runtime.Initialize() == aster::Status::kOk);
  assert(transport_module.AddIngress(9, Descriptor()) == aster::Status::kInvalidState);
  assert(runtime.Start() == aster::Status::kOk);

  const std::array payload{std::byte{1}, std::byte{2}};
  const aster::transport::PacketView packet{
      {8, aster::transport::PacketKind::kChannel, 1, 90, 200, Schema()}, payload};
  const aster::ExecutionContext caller("io", aster::ExecutionKind::kThread, 110);
  assert(transport.Inject(packet, caller) == aster::Status::kOk);
  assert(sink.size == payload.size());
  assert(sink.source_timestamp_ns == 90);

  executor.RunOne(clock);
  assert(transport.polls == 1);
  assert(transport_module.stats().polls == 1);
  runtime.Shutdown();
  assert(transport.Inject(packet, caller) == aster::Status::kInvalidState);
}

}  // namespace

int main() {
  OwnsTransportLifecycleAndRoutesIngress();
  return 0;
}
