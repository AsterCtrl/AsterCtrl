#include "aster/transport/can/socketcan.hpp"

#if defined(__linux__)

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace aster::transport::can {

SocketCan::~SocketCan() { Close(); }

Status SocketCan::Open(const char* interface_name) noexcept {
  if (interface_name == nullptr || interface_name[0] == '\0' || socket_ >= 0) {
    return socket_ >= 0 ? Status::kInvalidState : Status::kInvalidArgument;
  }
  const int descriptor = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (descriptor < 0) {
    return Status::kUnavailable;
  }

  ifreq request{};
  std::strncpy(request.ifr_name, interface_name, IFNAMSIZ - 1);
  if (::ioctl(descriptor, SIOCGIFINDEX, &request) < 0) {
    ::close(descriptor);
    return Status::kNotFound;
  }
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = request.ifr_ifindex;
  if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    ::close(descriptor);
    return Status::kUnavailable;
  }
  socket_ = descriptor;
  return Status::kOk;
}

Status SocketCan::Send(const CanFrame& frame) noexcept {
  if (socket_ < 0 || frame.size > kClassicCanPayloadSize || frame.arbitration_id > CAN_SFF_MASK) {
    return socket_ < 0 ? Status::kInvalidState : Status::kInvalidArgument;
  }
  can_frame native{};
  native.can_id = frame.arbitration_id;
  native.len = frame.size;
  for (std::size_t index = 0; index < frame.size; ++index) {
    native.data[index] = std::to_integer<std::uint8_t>(frame.data[index]);
  }
  while (true) {
    const auto written = ::write(socket_, &native, sizeof(native));
    if (written == static_cast<ssize_t>(sizeof(native))) {
      return Status::kOk;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    return errno == EAGAIN || errno == ENOBUFS ? Status::kCapacityExceeded : Status::kUnavailable;
  }
}

Status SocketCan::Receive(CanFrame& frame, std::uint32_t timeout_ms) noexcept {
  frame = {};
  if (socket_ < 0) {
    return Status::kInvalidState;
  }
  pollfd descriptor{socket_, POLLIN, 0};
  const auto ready = ::poll(&descriptor, 1, static_cast<int>(timeout_ms));
  if (ready == 0) {
    return Status::kUnavailable;
  }
  if (ready < 0 || (descriptor.revents & POLLIN) == 0) {
    return errno == EINTR ? Status::kUnavailable : Status::kInternal;
  }
  can_frame native{};
  const auto received = ::read(socket_, &native, sizeof(native));
  if (received != static_cast<ssize_t>(sizeof(native)) || (native.can_id & CAN_EFF_FLAG) != 0 ||
      native.len > CAN_MAX_DLEN) {
    return Status::kInvalidArgument;
  }
  frame.arbitration_id = static_cast<std::uint16_t>(native.can_id & CAN_SFF_MASK);
  frame.size = native.len;
  for (std::size_t index = 0; index < native.len; ++index) {
    frame.data[index] = static_cast<std::byte>(native.data[index]);
  }
  return Status::kOk;
}

void SocketCan::Close() noexcept {
  if (socket_ >= 0) {
    ::close(socket_);
    socket_ = -1;
  }
}

}  // namespace aster::transport::can

#else

namespace aster::transport::can {

SocketCan::~SocketCan() = default;

Status SocketCan::Open(const char*) noexcept { return Status::kUnavailable; }
Status SocketCan::Send(const CanFrame&) noexcept { return Status::kUnavailable; }
Status SocketCan::Receive(CanFrame& frame, std::uint32_t) noexcept {
  frame = {};
  return Status::kUnavailable;
}
void SocketCan::Close() noexcept { socket_ = -1; }

}  // namespace aster::transport::can

#endif
