#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "gpio.hpp"
#include "pwm.hpp"
#include "spi.hpp"
#include "uart.hpp"
#include "xrobot/runtime/execution_context.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::backend::libxr {

class UartResource {
 public:
  explicit UartResource(LibXR::UART& uart) noexcept : uart_(uart) {}

  static constexpr std::string_view TypeName() noexcept {
    return "xrobot.resource.Uart/v1";
  }
  LibXR::UART& get() const noexcept { return uart_; }

 private:
  LibXR::UART& uart_;
};

class SpiResource {
 public:
  explicit SpiResource(LibXR::SPI& spi) noexcept : spi_(spi) {}

  static constexpr std::string_view TypeName() noexcept {
    return "xrobot.resource.Spi/v1";
  }
  LibXR::SPI& get() const noexcept { return spi_; }

 private:
  LibXR::SPI& spi_;
};

class GpioResource {
 public:
  explicit GpioResource(LibXR::GPIO& gpio) noexcept : gpio_(gpio) {}

  static constexpr std::string_view TypeName() noexcept {
    return "xrobot.resource.Gpio/v1";
  }
  LibXR::GPIO& get() const noexcept { return gpio_; }

 private:
  LibXR::GPIO& gpio_;
};

class PwmResource {
 public:
  explicit PwmResource(LibXR::PWM& pwm) noexcept : pwm_(pwm) {}

  static constexpr std::string_view TypeName() noexcept {
    return "xrobot.resource.Pwm/v1";
  }
  LibXR::PWM& get() const noexcept { return pwm_; }

 private:
  LibXR::PWM& pwm_;
};

class ByteStreamEndpoint {
 public:
  virtual ~ByteStreamEndpoint() = default;

  static constexpr std::string_view TypeName() noexcept {
    return "xrobot.resource.ByteStream/v1";
  }

  virtual xrobot::runtime::Status Read(
      std::span<std::byte> output, std::size_t& bytes_read,
      std::uint64_t& completion_time_ns,
      const xrobot::runtime::ExecutionContext& caller) noexcept = 0;
  virtual xrobot::runtime::Status Write(
      std::span<const std::byte> input,
      const xrobot::runtime::ExecutionContext& caller) noexcept = 0;
  virtual xrobot::runtime::Status Poll(
      const xrobot::runtime::ExecutionContext& caller) noexcept = 0;
};

}  // namespace xrobot::backend::libxr
