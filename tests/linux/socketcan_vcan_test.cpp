#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>

#include "aster/execution.hpp"
#include "aster/transport/can/socketcan_adapter.hpp"

namespace {

struct Capture {
  aster::transport::can::CanFrame frame{};
  std::uint64_t receive_time_ns{};
  std::size_t calls{};
};

aster::Status CaptureFrame(void* state, const aster::transport::can::CanFrame& frame,
                           std::uint64_t receive_time_ns, const aster::ExecutionContext&) noexcept {
  auto& capture = *static_cast<Capture*>(state);
  capture.frame = frame;
  capture.receive_time_ns = receive_time_ns;
  ++capture.calls;
  return aster::Status::kOk;
}

}  // namespace

int main() {
  const char* const interface_name = std::getenv("ASTER_TEST_VCAN");
  if (interface_name == nullptr) {
    return 0;
  }

  aster::transport::can::SocketCan sender;
  aster::transport::can::SocketCanAdapter receiver(interface_name);
  Capture capture;
  assert(sender.Open(interface_name) == aster::Status::kOk);
  assert(receiver.Ready() == aster::Status::kOk);
  assert(receiver.Start({CaptureFrame, &capture}) == aster::Status::kOk);

  aster::transport::can::CanFrame sent;
  sent.arbitration_id = 0x321;
  sent.size = 3;
  sent.data[0] = std::byte{0x11};
  sent.data[1] = std::byte{0x22};
  sent.data[2] = std::byte{0x33};
  assert(sender.Send(sent) == aster::Status::kOk);

  const aster::ExecutionContext caller("vcan", aster::ExecutionKind::kThread, 42);
  aster::Status poll_status{aster::Status::kUnavailable};
  for (std::size_t attempt = 0; attempt < 100 && !aster::IsOk(poll_status); ++attempt) {
    poll_status = receiver.Poll(caller);
  }
  assert(poll_status == aster::Status::kOk);
  assert(capture.calls == 1);
  assert(capture.receive_time_ns == 42);
  assert(capture.frame.arbitration_id == sent.arbitration_id);
  assert(capture.frame.size == sent.size);
  for (std::size_t index = 0; index < sent.size; ++index) {
    assert(capture.frame.data[index] == sent.data[index]);
  }
  assert(receiver.Stop() == aster::Status::kOk);
  assert(!receiver.running());
  assert(receiver.Poll(caller) == aster::Status::kInvalidState);
}
