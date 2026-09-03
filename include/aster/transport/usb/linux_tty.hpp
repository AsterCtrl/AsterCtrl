#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/status.hpp"
#include "aster/transport/usb/byte_stream.hpp"

namespace aster::transport::usb {

class LinuxTtyStream final : public ByteStream {
 public:
  LinuxTtyStream() noexcept = default;
  ~LinuxTtyStream() override;

  LinuxTtyStream(const LinuxTtyStream&) = delete;
  LinuxTtyStream& operator=(const LinuxTtyStream&) = delete;

  Status Open(const char* path, std::uint32_t baud_rate) noexcept;
  Status Write(std::span<const std::byte> input, std::size_t& written) noexcept override;
  Status Read(std::span<std::byte> output, std::size_t& read) noexcept override;
  void Close() noexcept override;

  [[nodiscard]] bool open() const noexcept { return file_descriptor_ >= 0; }

 private:
  int file_descriptor_{-1};
};

}  // namespace aster::transport::usb
