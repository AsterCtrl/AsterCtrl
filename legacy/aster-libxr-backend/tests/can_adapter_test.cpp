#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "xrobot/backend/libxr/can_adapter.hpp"

namespace LibXR {

MicrosecondTimestamp Timebase::GetMicroseconds() {
  return MicrosecondTimestamp{0};
}

MillisecondTimestamp Timebase::GetMilliseconds() {
  return MillisecondTimestamp{0};
}

}  // namespace LibXR

namespace {

std::atomic<std::size_t> allocation_count{};

void* Allocate(std::size_t size, std::size_t alignment) {
  allocation_count.fetch_add(1, std::memory_order_relaxed);
  void* memory{};
  if (alignment <= alignof(std::max_align_t)) {
    memory = std::malloc(size);
  } else if (posix_memalign(&memory, alignment, size) != 0) {
    memory = nullptr;
  }
  if (memory == nullptr) {
    throw std::bad_alloc();
  }
  return memory;
}

}  // namespace

void* operator new(std::size_t size) { return Allocate(size, alignof(std::max_align_t)); }
void* operator new[](std::size_t size) {
  return Allocate(size, alignof(std::max_align_t));
}
void* operator new(std::size_t size, std::align_val_t alignment) {
  return Allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return Allocate(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
  std::free(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
  std::free(memory);
}

namespace {

using xrobot::backend::libxr::CanAdapter;
using xrobot::backend::libxr::LibxrClassicCanEndpoint;
using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::Status;
using xrobot::transport::can::CanArbitrationId;
using xrobot::transport::can::CanFrame;
using xrobot::transport::can::CanPriority;

class FakeCan final : public LibXR::CAN {
 public:
  LibXR::ErrorCode SetConfig(const Configuration&) override {
    return LibXR::ErrorCode::OK;
  }
  std::uint32_t GetClockFreq() const override { return 48'000'000; }
  LibXR::ErrorCode AddMessage(const ClassicPack& pack) override {
    last_transmitted = pack;
    return next_send_status;
  }

  void Emit(const ClassicPack& pack, bool in_isr = true) {
    OnMessage(pack, in_isr);
  }

  ClassicPack last_transmitted{};
  LibXR::ErrorCode next_send_status{LibXR::ErrorCode::OK};
};

struct Clock {
  std::uint64_t now_ns{123'456};
};

std::uint64_t ReadClock(void* state) noexcept {
  return static_cast<Clock*>(state)->now_ns;
}

struct Receiver {
  CanFrame frame;
  std::uint64_t receive_time_ns{};
  std::uint32_t count{};
};

Status Receive(void* state, const CanFrame& frame,
               std::uint64_t receive_time_ns,
               const ExecutionContext& context) noexcept {
  assert(context.kind() == ExecutionKind::kThread);
  auto& receiver = *static_cast<Receiver*>(state);
  receiver.frame = frame;
  receiver.receive_time_ns = receive_time_ns;
  ++receiver.count;
  return Status::kOk;
}

LibXR::CAN::ClassicPack MakePack(std::uint16_t route_id,
                                 std::uint8_t value) {
  std::uint16_t arbitration_id{};
  assert(CanArbitrationId::Encode(CanPriority::kControl, route_id,
                                  arbitration_id) == Status::kOk);
  LibXR::CAN::ClassicPack pack{};
  pack.id = arbitration_id;
  pack.type = LibXR::CAN::Type::STANDARD;
  pack.dlc = 2;
  pack.data[0] = 0;
  pack.data[1] = value;
  return pack;
}

void AdapterDefersIsrReceiveAndWritesThroughLibxr() {
  FakeCan can;
  Clock clock;
  Receiver receiver;
  LibxrClassicCanEndpoint<2> endpoint(can, {ReadClock, &clock});
  CanAdapter<2> adapter(endpoint);
  const ExecutionContext thread_context("can-rx", ExecutionKind::kThread, 3);
  const ExecutionContext interrupt_context("can-rx", ExecutionKind::kInterrupt,
                                           3);

  assert(adapter.Initialize() == Status::kInvalidState);
  assert(adapter.BindReceiver({}) == Status::kInvalidArgument);
  assert(adapter.BindReceiver({Receive, &receiver}) == Status::kOk);
  assert(adapter.BindReceiver({Receive, &receiver}) == Status::kInvalidState);
  assert(adapter.Initialize() == Status::kOk);
  assert(adapter.Initialize() == Status::kInvalidState);
  assert(adapter.BindReceiver({Receive, &receiver}) == Status::kInvalidState);
  assert(endpoint.Initialize() == Status::kOk);
  const auto allocations_after_init =
      allocation_count.load(std::memory_order_relaxed);

  const auto first = MakePack(8, 0x42);
  can.Emit(first);
  assert(receiver.count == 0);
  assert(adapter.Drain(interrupt_context) == Status::kInvalidArgument);
  assert(adapter.Drain(thread_context, 1) == Status::kOk);
  assert(receiver.count == 1);
  assert(receiver.receive_time_ns == clock.now_ns);
  assert(receiver.frame.data[1] == std::byte{0x42});

  CanFrame outgoing;
  outgoing.arbitration_id = static_cast<std::uint16_t>(first.id);
  outgoing.size = 2;
  outgoing.data[0] = std::byte{0x01};
  outgoing.data[1] = std::byte{0xa5};
  assert(adapter.writer().Send(outgoing, thread_context) == Status::kOk);
  assert(can.last_transmitted.id == first.id);
  assert(can.last_transmitted.data[1] == 0xa5);
  assert(allocation_count.load(std::memory_order_relaxed) ==
         allocations_after_init);

  const auto stats = adapter.stats();
  assert(stats.rx_frames == 1);
  assert(stats.dispatched == 1);
  assert(stats.tx_frames == 1);
}

void AdapterBoundsQueueAndMapsDriverBackpressure() {
  FakeCan can;
  Clock clock;
  Receiver receiver;
  LibxrClassicCanEndpoint<2> endpoint(can, {ReadClock, &clock});
  CanAdapter<2> adapter(endpoint);
  const ExecutionContext context("can-rx", ExecutionKind::kThread, 3);
  assert(adapter.BindReceiver({Receive, &receiver}) == Status::kOk);
  assert(adapter.Initialize() == Status::kOk);
  assert(endpoint.Initialize() == Status::kOk);

  can.Emit(MakePack(8, 1));
  can.Emit(MakePack(8, 2));
  can.Emit(MakePack(8, 3));
  assert(adapter.stats().rx_dropped == 1);
  assert(adapter.Drain(context) == Status::kOk);
  assert(receiver.count == 2);

  CanFrame outgoing;
  outgoing.arbitration_id = static_cast<std::uint16_t>(MakePack(8, 0).id);
  outgoing.size = 1;
  can.next_send_status = LibXR::ErrorCode::FULL;
  assert(adapter.writer().Send(outgoing, context) == Status::kCapacityExceeded);
  assert(adapter.stats().tx_failures == 1);
}

}  // namespace

int main() {
  AdapterDefersIsrReceiveAndWritesThroughLibxr();
  AdapterBoundsQueueAndMapsDriverBackpressure();
  return 0;
}
