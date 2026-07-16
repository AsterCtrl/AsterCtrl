#include "aster/backend/libxr/stm32_usbd_cdc_endpoint.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

using aster::backend::libxr::Stm32UsbdCdcDriver;
using aster::backend::libxr::Stm32UsbdCdcEndpoint;
using aster::runtime::ExecutionContext;
using aster::runtime::ExecutionKind;
using aster::runtime::Status;

class FakeClock final : public aster::runtime::SteadyClock {
 public:
  std::uint64_t NowNs() const noexcept override { return now_ns; }
  std::uint64_t now_ns{};
};

struct FakeDriver {
  static std::uint8_t* Register(Stm32UsbdCdcDriver::Callback tx,
                                Stm32UsbdCdcDriver::Callback rx) {
    tx_callback = tx;
    rx_callback = rx;
    return receive_buffer.data();
  }

  static std::uint8_t Transmit(std::uint8_t* data, std::uint16_t size) {
    transmitted_data = data;
    transmitted_size = size;
    ++transmit_calls;
    return transmit_result;
  }

  inline static std::array<std::uint8_t, 64> receive_buffer{};
  inline static Stm32UsbdCdcDriver::Callback tx_callback{};
  inline static Stm32UsbdCdcDriver::Callback rx_callback{};
  inline static std::uint8_t* transmitted_data{};
  inline static std::uint16_t transmitted_size{};
  inline static std::uint32_t transmit_calls{};
  inline static std::uint8_t transmit_result{};
};

void CopiesReceivePacketsAndOwnsTransmitBytesUntilCompletion() {
  FakeClock clock;
  Stm32UsbdCdcEndpoint<64U, 2U, 2U> endpoint(
      {.register_callbacks = FakeDriver::Register,
       .transmit = FakeDriver::Transmit,
       .receive_buffer_size = FakeDriver::receive_buffer.size(),
       .success_code = 0U,
       .busy_code = 1U},
      clock);
  const ExecutionContext execution("usb", ExecutionKind::kThread, 5U);
  assert(endpoint.Initialize() == Status::kOk);

  FakeDriver::receive_buffer[0] = 0xa5U;
  FakeDriver::receive_buffer[1] = 0x5aU;
  clock.now_ns = 42'000U;
  FakeDriver::rx_callback(2U);
  FakeDriver::receive_buffer[0] = 0U;
  FakeDriver::receive_buffer[0] = 0x11U;
  clock.now_ns = 43'000U;
  FakeDriver::rx_callback(1U);
  FakeDriver::rx_callback(1U);

  std::array<std::byte, 64> output{};
  std::size_t bytes_read{};
  std::uint64_t completion_time_ns{};
  assert(endpoint.Read(output, bytes_read, completion_time_ns, execution) ==
         Status::kOk);
  assert(bytes_read == 2U);
  assert(completion_time_ns == 42'000U);
  assert(output[0] == std::byte{0xa5U});
  assert(endpoint.Read(output, bytes_read, completion_time_ns, execution) ==
         Status::kOk);
  assert(output[0] == std::byte{0x11U});
  assert(endpoint.stats().rx_dropped == 1U);

  std::array<std::byte, 3> frame{
      std::byte{1U}, std::byte{2U}, std::byte{3U}};
  assert(endpoint.Write(frame, execution) == Status::kOk);
  const std::array<std::byte, 2> second_frame{
      std::byte{4U}, std::byte{5U}};
  assert(endpoint.Write(second_frame, execution) == Status::kOk);
  assert(endpoint.Write(frame, execution) == Status::kCapacityExceeded);
  frame.fill(std::byte{0xffU});
  assert(endpoint.Poll(execution) == Status::kOk);
  assert(FakeDriver::transmitted_size == 3U);
  assert(FakeDriver::transmitted_data[0] == 1U);
  assert(endpoint.Poll(execution) == Status::kUnavailable);
  FakeDriver::tx_callback(3U);
  assert(endpoint.stats().tx_completed == 1U);
  assert(endpoint.Poll(execution) == Status::kOk);
  assert(FakeDriver::transmitted_size == 2U);
  assert(FakeDriver::transmitted_data[0] == 4U);
  FakeDriver::tx_callback(2U);
  assert(endpoint.Poll(execution) == Status::kUnavailable);
  assert(endpoint.stats().tx_completed == 2U);
  assert(endpoint.stats().tx_dropped == 1U);
}

}  // namespace

int main() {
  CopiesReceivePacketsAndOwnsTransmitBytesUntilCompletion();
  return 0;
}
