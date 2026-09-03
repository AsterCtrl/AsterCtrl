#pragma once

#include <cstdint>

#include "aster/status.hpp"
#include "aster/transport/can/protocol.hpp"

namespace aster::transport::can {

class SocketCan {
 public:
  SocketCan() noexcept = default;
  // Out-of-line because the Linux implementation closes an owned descriptor.
  ~SocketCan();  // NOLINT(performance-trivially-destructible)

  SocketCan(const SocketCan&) = delete;
  SocketCan& operator=(const SocketCan&) = delete;

  Status Open(const char* interface_name) noexcept;
  Status Send(const CanFrame& frame) noexcept;
  Status Receive(CanFrame& frame, std::uint32_t timeout_ms = 0) noexcept;
  void Close() noexcept;

  [[nodiscard]] bool open() const noexcept { return socket_ >= 0; }

 private:
  int socket_{-1};
};

}  // namespace aster::transport::can
