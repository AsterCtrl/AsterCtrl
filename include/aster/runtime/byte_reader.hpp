#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "aster/runtime/execution_context.hpp"
#include "aster/runtime/status.hpp"

namespace aster::runtime {

class ByteReader {
 public:
  static constexpr std::string_view TypeName() noexcept {
    return "aster.hardware.ByteReader/v1";
  }

  virtual ~ByteReader() = default;

  // Reads only bytes already available in the adapter's bounded RX queue.
  virtual Status Read(std::span<std::byte> destination,
                      std::size_t& bytes_read,
                      const ExecutionContext& caller) noexcept = 0;

  // Discards bytes currently queued in software; it does not restart hardware.
  virtual Status Discard(const ExecutionContext& caller) noexcept = 0;
};

}  // namespace aster::runtime
