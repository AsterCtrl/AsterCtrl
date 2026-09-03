#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>

#include "aster/transport/can/socketcan.hpp"

int main() {
  const char* const interface_name = std::getenv("ASTER_TEST_VCAN");
  if (interface_name == nullptr) {
    return 0;
  }

  aster::transport::can::SocketCan sender;
  aster::transport::can::SocketCan receiver;
  assert(sender.Open(interface_name) == aster::Status::kOk);
  assert(receiver.Open(interface_name) == aster::Status::kOk);

  aster::transport::can::CanFrame sent;
  sent.arbitration_id = 0x321;
  sent.size = 3;
  sent.data[0] = std::byte{0x11};
  sent.data[1] = std::byte{0x22};
  sent.data[2] = std::byte{0x33};
  assert(sender.Send(sent) == aster::Status::kOk);

  aster::transport::can::CanFrame received;
  assert(receiver.Receive(received, 100) == aster::Status::kOk);
  assert(received.arbitration_id == sent.arbitration_id);
  assert(received.size == sent.size);
  for (std::size_t index = 0; index < sent.size; ++index) {
    assert(received.data[index] == sent.data[index]);
  }
}
