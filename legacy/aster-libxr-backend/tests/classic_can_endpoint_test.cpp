#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "xrobot/backend/libxr/classic_can_endpoint.hpp"

namespace LibXR {

MicrosecondTimestamp Timebase::GetMicroseconds() {
  return MicrosecondTimestamp{0};
}

MillisecondTimestamp Timebase::GetMilliseconds() {
  return MillisecondTimestamp{0};
}

}  // namespace LibXR

namespace {

using xrobot::backend::libxr::ClassicCanFrame;
using xrobot::backend::libxr::LibxrClassicCanEndpoint;
using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::Status;

class FakeCan final : public LibXR::CAN {
 public:
  LibXR::ErrorCode SetConfig(const Configuration&) override {
    return LibXR::ErrorCode::OK;
  }
  std::uint32_t GetClockFreq() const override { return 48'000'000U; }
  LibXR::ErrorCode AddMessage(const ClassicPack& pack) override {
    transmitted = pack;
    return send_status;
  }

  void Emit(const ClassicPack& pack) { OnMessage(pack, true); }

  ClassicPack transmitted{};
  LibXR::ErrorCode send_status{LibXR::ErrorCode::OK};
};

struct Clock {
  std::uint64_t now_ns{42'000U};
};

std::uint64_t ReadClock(void* state) noexcept {
  return static_cast<Clock*>(state)->now_ns;
}

struct Recorder {
  ClassicCanFrame frame{};
  std::uint64_t timestamp_ns{};
  std::uint32_t deliveries{};
};

void Record(void* state, const ClassicCanFrame& frame,
            std::uint64_t timestamp_ns, bool in_interrupt) noexcept {
  assert(in_interrupt);
  auto& recorder = *static_cast<Recorder*>(state);
  recorder.frame = frame;
  recorder.timestamp_ns = timestamp_ns;
  ++recorder.deliveries;
}

LibXR::CAN::ClassicPack Pack(std::uint16_t id, std::uint8_t value) {
  LibXR::CAN::ClassicPack pack{};
  pack.id = id;
  pack.type = LibXR::CAN::Type::STANDARD;
  pack.dlc = 1;
  pack.data[0] = value;
  return pack;
}

void FansOutMatchingSubscriptionsAndWrites() {
  FakeCan can;
  Clock clock;
  Recorder exact;
  Recorder range;
  LibxrClassicCanEndpoint<2> endpoint(can, {ReadClock, &clock});
  const ExecutionContext thread("can", ExecutionKind::kThread, 4);

  assert(endpoint.Subscribe(0x201U, 0x201U, {Record, &exact}) == Status::kOk);
  assert(endpoint.Subscribe(0x200U, 0x20fU, {Record, &range}) == Status::kOk);
  assert(endpoint.Subscribe(0x100U, 0x100U, {Record, &range}) ==
         Status::kCapacityExceeded);
  assert(endpoint.Initialize() == Status::kOk);
  assert(endpoint.Initialize() == Status::kInvalidState);
  assert(endpoint.Subscribe(0x202U, 0x202U, {Record, &range}) ==
         Status::kInvalidState);

  can.Emit(Pack(0x201U, 0xa5U));
  assert(exact.deliveries == 1U);
  assert(range.deliveries == 1U);
  assert(exact.timestamp_ns == clock.now_ns);
  assert(exact.frame.data[0] == std::byte{0xa5});

  ClassicCanFrame frame{};
  frame.id = 0x123U;
  frame.size = 2U;
  frame.data[0] = std::byte{0x11};
  frame.data[1] = std::byte{0x22};
  assert(endpoint.Write(frame, thread) == Status::kOk);
  assert(can.transmitted.id == frame.id);
  assert(can.transmitted.data[1] == 0x22U);

  can.send_status = LibXR::ErrorCode::FULL;
  assert(endpoint.Write(frame, thread) == Status::kCapacityExceeded);
  const auto stats = endpoint.stats();
  assert(stats.rx_frames == 1U);
  assert(stats.rx_deliveries == 2U);
  assert(stats.tx_frames == 1U);
  assert(stats.tx_failures == 1U);
}

void RejectsInvalidFramesAndInterruptWrites() {
  FakeCan can;
  Clock clock;
  Recorder recorder;
  LibxrClassicCanEndpoint<1> endpoint(can, {ReadClock, &clock});
  const ExecutionContext interrupt("can", ExecutionKind::kInterrupt, 4);

  assert(endpoint.Subscribe(0U, 0x7ffU, {Record, &recorder}) == Status::kOk);
  assert(endpoint.Initialize() == Status::kOk);

  auto invalid = Pack(0x100U, 1U);
  invalid.dlc = 0U;
  can.Emit(invalid);
  assert(recorder.deliveries == 0U);

  ClassicCanFrame frame{};
  frame.id = 0x100U;
  frame.size = 1U;
  assert(endpoint.Write(frame, interrupt) == Status::kInvalidArgument);
  assert(endpoint.stats().rx_invalid == 1U);
}

}  // namespace

int main() {
  FansOutMatchingSubscriptionsAndWrites();
  RejectsInvalidFramesAndInterruptWrites();
  return 0;
}
