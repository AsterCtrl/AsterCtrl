#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/execution.hpp"
#include "aster/status.hpp"
#include "aster/type_support.hpp"

namespace aster::transport {

enum class PacketKind : std::uint8_t {
  kChannel = 1,
  kRpcRequest = 2,
  kRpcResponse = 3,
  kControl = 4,
};

struct PacketHeader {
  std::uint16_t route_id{};
  PacketKind kind{PacketKind::kChannel};
  std::uint32_t sequence{};
  std::uint64_t source_timestamp_ns{};
  std::uint64_t deadline_ns{};
  SchemaHash schema_hash{};
};

struct PacketView {
  PacketHeader header{};
  std::span<const std::byte> payload;
};

struct TransportStats {
  std::uint32_t packets_sent{};
  std::uint32_t packets_received{};
  std::uint32_t bytes_sent{};
  std::uint32_t bytes_received{};
  std::uint32_t backpressure{};
  std::uint32_t invalid_packets{};
  std::uint32_t deadline_misses{};
};

using PacketReceiver = Status (*)(void*, const PacketView&, const ExecutionContext&) noexcept;

class Transport {
 public:
  virtual ~Transport() = default;

  virtual Status Start(PacketReceiver receiver, void* receiver_state) noexcept = 0;
  virtual Status Send(const PacketView& packet, const ExecutionContext& caller) noexcept = 0;
  virtual Status Poll(const ExecutionContext& caller) noexcept = 0;
  virtual void Stop() noexcept = 0;
  [[nodiscard]] virtual TransportStats stats() const noexcept = 0;
};

}  // namespace aster::transport
