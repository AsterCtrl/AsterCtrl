#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/channel.hpp"
#include "aster/core_ref.hpp"
#include "aster/executor.hpp"
#include "aster/transport/can/channel_transport_module.hpp"

namespace {

class ManualClock final : public aster::Clock {
 public:
  [[nodiscard]] aster::ClockDomain domain() const noexcept override {
    return aster::ClockDomain::kMonotonic;
  }
  [[nodiscard]] std::uint64_t NowNs() const noexcept override { return now_ns; }

  std::uint64_t now_ns{1'000'000};
};

class QueuedExecutor final : public aster::Executor {
 public:
  [[nodiscard]] std::string_view Name() const noexcept override { return "can"; }

  aster::Status TryPost(aster::WorkItem work,
                        const aster::ExecutionContext& caller) noexcept override {
    return TryPostAt(caller.timestamp_ns(), work, caller);
  }

  aster::Status TryPostAt(std::uint64_t timestamp_ns, aster::WorkItem work,
                          const aster::ExecutionContext&) noexcept override {
    if (!work || size_ == entries_.size()) {
      return !work ? aster::Status::kInvalidArgument : aster::Status::kCapacityExceeded;
    }
    entries_[size_++] = {timestamp_ns, work};
    return aster::Status::kOk;
  }

  void RunReady(const ManualClock& clock) noexcept {
    std::size_t index{};
    while (index < size_) {
      if (entries_[index].timestamp_ns > clock.now_ns) {
        ++index;
        continue;
      }
      const auto work = entries_[index].work;
      for (std::size_t move = index + 1; move < size_; ++move) {
        entries_[move - 1] = entries_[move];
      }
      --size_;
      work.Run({"can", aster::ExecutionKind::kThread, clock.now_ns});
    }
  }

 private:
  struct Entry {
    std::uint64_t timestamp_ns{};
    aster::WorkItem work{};
  };

  std::array<Entry, 8> entries_{};
  std::size_t size_{};
};

class TestAdapter {
 public:
  void Connect(TestAdapter& peer) noexcept { peer_ = &peer; }

  [[nodiscard]] aster::Status Ready() const noexcept {
    return peer_ == nullptr ? aster::Status::kUnavailable : aster::Status::kOk;
  }

  aster::Status Start(aster::transport::can::CanFrameReceiver receiver) noexcept {
    if (running_ || receiver.receive == nullptr) {
      return running_ ? aster::Status::kInvalidState : aster::Status::kInvalidArgument;
    }
    receiver_ = receiver;
    running_ = true;
    return aster::Status::kOk;
  }

  aster::Status Send(const aster::transport::can::CanFrame& frame,
                     const aster::ExecutionContext&) noexcept {
    if (!running_ || peer_ == nullptr || peer_->size_ == peer_->frames_.size()) {
      return !running_ ? aster::Status::kInvalidState : aster::Status::kCapacityExceeded;
    }
    peer_->frames_[peer_->size_++] = frame;
    return aster::Status::kOk;
  }

  aster::Status Poll(const aster::ExecutionContext& caller) noexcept {
    if (!running_) {
      return aster::Status::kInvalidState;
    }
    if (size_ == 0) {
      return aster::Status::kUnavailable;
    }
    const auto count = size_;
    size_ = 0;
    auto result = aster::Status::kOk;
    for (std::size_t index = 0; index < count; ++index) {
      const auto status = receiver_.Accept(frames_[index], caller.timestamp_ns(), caller);
      if (!aster::IsOk(status) && status != aster::Status::kUnavailable) {
        result = status;
      }
    }
    return result;
  }

  aster::Status Stop() noexcept {
    running_ = false;
    receiver_ = {};
    size_ = 0;
    return aster::Status::kOk;
  }

  [[nodiscard]] aster::transport::can::CanFrameWriter writer() noexcept { return {Write, this}; }

 private:
  static aster::Status Write(void* state, const aster::transport::can::CanFrame& frame,
                             const aster::ExecutionContext& caller) noexcept {
    return static_cast<TestAdapter*>(state)->Send(frame, caller);
  }

  TestAdapter* peer_{};
  aster::transport::can::CanFrameReceiver receiver_{};
  std::array<aster::transport::can::CanFrame, 64> frames_{};
  std::size_t size_{};
  bool running_{};
};

constexpr aster::SchemaHash Schema() {
  aster::SchemaHash hash{};
  hash.bytes[0] = std::byte{0x45};
  return hash;
}

constexpr aster::ChannelDescriptor Descriptor() {
  return {"state", {"example.State", Schema(), 8}};
}

aster::transport::can::CanLinkControlConfig Control(std::uint8_t local, std::uint8_t peer,
                                                    bool authority) {
  aster::transport::can::Handshake handshake;
  handshake.protocol_version = 1;
  handshake.node_id = local;
  handshake.deployment_hash.fill(std::byte{0x12});
  handshake.schema_hash.fill(std::byte{0x34});
  return {
      .local = handshake,
      .peer_node_id = peer,
      .time_authority = authority,
      .handshake_period_ns = 1'000'000'000,
      .heartbeat_period_ns = 100'000'000,
      .heartbeat_timeout_ns = 300'000'000,
      .time_sync_period_ns = 10'000'000,
      .recovery_samples = 1,
      .retry_timeout_ns = 5'000'000,
      .maximum_retries = 2,
      .reassembly_timeout_ns = 100'000'000,
  };
}

struct Capture {
  std::array<std::byte, 8> bytes{};
  std::size_t size{};
};

aster::Status Receive(void* state, std::span<const std::byte> bytes, const aster::MessageInfo&,
                      const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  capture.size = bytes.size();
  std::copy(bytes.begin(), bytes.end(), capture.bytes.begin());
  return aster::Status::kOk;
}

aster::CoreRef Core(QueuedExecutor& executor, aster::LocalChannel<1, 1, 8>& channel,
                    ManualClock& clock) {
  auto handles = aster::CoreHandles{};
  handles.executor = aster::ExecutorRef(executor);
  handles.channel = aster::ChannelRef(channel);
  handles.clock = aster::ClockRef(clock);
  return aster::CoreRef(handles);
}

void HandshakesAndRoutesAReliableChannel() {
  ManualClock clock;
  QueuedExecutor source_executor;
  QueuedExecutor destination_executor;
  aster::LocalChannel<1, 1, 8> source_channel;
  aster::LocalChannel<1, 1, 8> destination_channel;
  TestAdapter source_adapter;
  TestAdapter destination_adapter;
  source_adapter.Connect(destination_adapter);
  destination_adapter.Connect(source_adapter);

  using Module = aster::transport::can::CanChannelTransportModule<TestAdapter, 1, 1, 8>;
  Module source("can0", source_adapter, Control(1, 2, true), 1'000'000);
  Module destination("can0", destination_adapter, Control(2, 1, false), 1'000'000);
  Capture capture;

  assert(source_channel.RegisterPublisher(Descriptor()) == aster::Status::kOk);
  assert(source.AddEgress(8, Descriptor(), aster::transport::can::ChannelReliability::kReliable) ==
         aster::Status::kOk);
  assert(destination.AddIngress(8, Descriptor(),
                                aster::transport::can::ChannelReliability::kReliable) ==
         aster::Status::kOk);
  assert(destination_channel.RegisterSubscriber(Descriptor(), Receive, &capture) ==
         aster::Status::kOk);
  assert(source.Initialize(Core(source_executor, source_channel, clock)) == aster::Status::kOk);
  assert(destination.Initialize(Core(destination_executor, destination_channel, clock)) ==
         aster::Status::kOk);
  assert(source_channel.Seal() == aster::Status::kOk);
  assert(destination_channel.Seal() == aster::Status::kOk);
  assert(source.Start() == aster::Status::kOk);
  assert(destination.Start() == aster::Status::kOk);

  for (std::size_t iteration = 0; iteration < 6; ++iteration) {
    source_executor.RunReady(clock);
    destination_executor.RunReady(clock);
    clock.now_ns += 1'000'000;
  }
  assert(source.control().application_enabled());
  assert(destination.control().application_enabled());

  const std::array message{std::byte{1}, std::byte{2}, std::byte{3}};
  const aster::ExecutionContext caller("control", aster::ExecutionKind::kThread, clock.now_ns);
  assert(source_channel.Publish(Descriptor(), message, clock.now_ns, caller) == aster::Status::kOk);
  destination_executor.RunReady(clock);
  source_executor.RunReady(clock);
  assert(capture.size == message.size());
  assert(std::equal(message.begin(), message.end(), capture.bytes.begin()));

  source.Shutdown();
  destination.Shutdown();
}

}  // namespace

int main() {
  HandshakesAndRoutesAReliableChannel();
  return 0;
}
