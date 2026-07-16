#include "aster/backend/libxr/uart_reader_adapter.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace LibXR {

MicrosecondTimestamp Timebase::GetMicroseconds() {
  return MicrosecondTimestamp{0};
}

MillisecondTimestamp Timebase::GetMilliseconds() {
  return MillisecondTimestamp{0};
}

}  // namespace LibXR

namespace {

using aster::backend::libxr::UartReaderAdapter;
using aster::runtime::ExecutionContext;
using aster::runtime::ExecutionKind;
using aster::runtime::Status;

class FakeUart final : public LibXR::UART {
 public:
  FakeUart() : LibXR::UART(&read_port, &write_port) {
    read_port = NotifyRead;
  }

  LibXR::ErrorCode SetConfig(Configuration) override {
    return LibXR::ErrorCode::OK;
  }

  void Push(std::span<const std::uint8_t> bytes) {
    assert(read_port.queue_data_->PushBatch(bytes.data(), bytes.size()) ==
           LibXR::ErrorCode::OK);
    read_port.ProcessPendingReads(false);
  }

  static LibXR::ErrorCode NotifyRead(LibXR::ReadPort&, bool) {
    return LibXR::ErrorCode::PENDING;
  }

  LibXR::ReadPort read_port{32};
  LibXR::WritePort write_port{1, 1};
};

void ReadsOnlyAvailableBytesAndReportsCompletionSynchronously() {
  FakeUart uart;
  UartReaderAdapter reader(uart);
  const ExecutionContext thread("uart-rx", ExecutionKind::kThread, 5);
  std::array<std::byte, 4> output{};
  std::size_t size{99};

  assert(reader.Read(output, size, thread) == Status::kUnavailable);
  assert(size == 0U);
  const std::array<std::uint8_t, 5> input{1, 2, 3, 4, 5};
  uart.Push(input);
  assert(reader.Read(output, size, thread) == Status::kOk);
  assert(size == output.size());
  assert(output[0] == std::byte{1});
  assert(output[3] == std::byte{4});
  assert(reader.Read(output, size, thread) == Status::kOk);
  assert(size == 1U);
  assert(output[0] == std::byte{5});

  const auto stats = reader.stats();
  assert(stats.read_calls == 3U);
  assert(stats.bytes_read == 5U);
  assert(stats.empty_reads == 1U);
}

void RejectsInterruptReadsAndCanDiscardQueuedInput() {
  FakeUart uart;
  UartReaderAdapter reader(uart);
  const ExecutionContext thread("uart-rx", ExecutionKind::kThread, 5);
  const ExecutionContext interrupt("uart-rx", ExecutionKind::kInterrupt, 5);
  std::array<std::byte, 4> output{};
  std::size_t size{};
  const std::array<std::uint8_t, 3> input{7, 8, 9};
  uart.Push(input);

  assert(reader.Read(output, size, interrupt) == Status::kInvalidArgument);
  assert(size == 0U);
  assert(reader.Discard(interrupt) == Status::kInvalidArgument);
  assert(reader.Discard(thread) == Status::kOk);
  assert(reader.Read(output, size, thread) == Status::kUnavailable);
  assert(reader.stats().discards == 1U);
  assert(reader.stats().failures == 2U);
}

}  // namespace

int main() {
  ReadsOnlyAvailableBytesAndReportsCompletionSynchronously();
  RejectsInterruptReadsAndCanDiscardQueuedInput();
  return 0;
}
