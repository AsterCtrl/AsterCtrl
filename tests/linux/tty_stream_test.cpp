#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <span>

#include "aster/transport/usb/linux_tty.hpp"

int main() {
  const int master = posix_openpt(O_RDWR | O_NOCTTY);
  assert(master >= 0);
  assert(grantpt(master) == 0);
  assert(unlockpt(master) == 0);
  const char* const slave = ptsname(master);
  assert(slave != nullptr);

  aster::transport::usb::LinuxTtyStream stream;
  assert(stream.Open(slave, 115200) == aster::Status::kOk);

  const std::array outbound{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  std::size_t written{};
  assert(stream.Write(outbound, written) == aster::Status::kOk);
  assert(written == outbound.size());
  std::array<std::byte, 3> observed{};
  assert(::read(master, observed.data(), observed.size()) == static_cast<ssize_t>(observed.size()));
  assert(observed == outbound);

  const std::array inbound{std::byte{0x44}, std::byte{0x55}};
  assert(::write(master, inbound.data(), inbound.size()) == static_cast<ssize_t>(inbound.size()));
  std::array<std::byte, 4> received{};
  std::size_t read{};
  assert(stream.Read(received, read) == aster::Status::kOk);
  assert(read == inbound.size());
  assert(std::equal(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(read),
                    inbound.begin(), inbound.end()));

  stream.Close();
  ::close(master);
}
