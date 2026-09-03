#include "aster/transport/usb/linux_tty.hpp"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace aster::transport::usb {
namespace {

speed_t Baud(std::uint32_t baud_rate) noexcept {
  switch (baud_rate) {
    case 9600:
      return B9600;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
#ifdef B230400
    case 230400:
      return B230400;
#endif
#ifdef B460800
    case 460800:
      return B460800;
#endif
#ifdef B921600
    case 921600:
      return B921600;
#endif
    default:
      return 0;
  }
}

}  // namespace

LinuxTtyStream::~LinuxTtyStream() { Close(); }

Status LinuxTtyStream::Open(const char* path, std::uint32_t baud_rate) noexcept {
  if (path == nullptr || file_descriptor_ >= 0) {
    return path == nullptr ? Status::kInvalidArgument : Status::kInvalidState;
  }
  const auto speed = Baud(baud_rate);
  if (speed == 0) {
    return Status::kInvalidArgument;
  }
  const int descriptor = ::open(path, O_RDWR | O_NOCTTY);
  if (descriptor < 0) {
    return Status::kNotFound;
  }
  termios configuration{};
  if (tcgetattr(descriptor, &configuration) != 0) {
    ::close(descriptor);
    return Status::kInternal;
  }
  cfmakeraw(&configuration);
  configuration.c_cflag |= CLOCAL | CREAD;
  if (cfsetispeed(&configuration, speed) != 0 || cfsetospeed(&configuration, speed) != 0 ||
      tcsetattr(descriptor, TCSANOW, &configuration) != 0) {
    ::close(descriptor);
    return Status::kInternal;
  }
  file_descriptor_ = descriptor;
  return Status::kOk;
}

Status LinuxTtyStream::Write(std::span<const std::byte> input, std::size_t& written) noexcept {
  written = 0;
  if (file_descriptor_ < 0) {
    return Status::kInvalidState;
  }
  while (written < input.size()) {
    const auto result = ::write(file_descriptor_, input.data() + written, input.size() - written);
    if (result > 0) {
      written += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return Status::kUnavailable;
  }
  return Status::kOk;
}

Status LinuxTtyStream::Read(std::span<std::byte> output, std::size_t& read) noexcept {
  read = 0;
  if (file_descriptor_ < 0 || output.empty()) {
    return file_descriptor_ < 0 ? Status::kInvalidState : Status::kInvalidArgument;
  }
  pollfd descriptor{file_descriptor_, POLLIN, 0};
  const auto ready = ::poll(&descriptor, 1, 0);
  if (ready < 0) {
    return errno == EINTR ? Status::kUnavailable : Status::kInternal;
  }
  if (ready == 0 || (descriptor.revents & POLLIN) == 0) {
    return Status::kUnavailable;
  }
  const auto result = ::read(file_descriptor_, output.data(), output.size());
  if (result < 0) {
    return errno == EINTR || errno == EAGAIN ? Status::kUnavailable : Status::kInternal;
  }
  read = static_cast<std::size_t>(result);
  return Status::kOk;
}

void LinuxTtyStream::Close() noexcept {
  if (file_descriptor_ >= 0) {
    ::close(file_descriptor_);
    file_descriptor_ = -1;
  }
}

}  // namespace aster::transport::usb
