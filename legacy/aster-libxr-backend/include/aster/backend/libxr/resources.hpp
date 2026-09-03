#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "gpio.hpp"
#include "pwm.hpp"
#include "spi.hpp"
#include "uart.hpp"
#include "aster/runtime/execution_context.hpp"
#include "aster/runtime/status.hpp"

namespace aster::backend::libxr {

class UartResource {
 public:
  explicit UartResource(LibXR::UART& uart) noexcept : uart_(uart) {}

  static constexpr std::string_view TypeName() noexcept {
    return "aster.resource.Uart/v1";
  }
  LibXR::UART& get() const noexcept { return uart_; }

 private:
  LibXR::UART& uart_;
};

class SpiResource {
 public:
  explicit SpiResource(LibXR::SPI& spi) noexcept : spi_(spi) {}

  static constexpr std::string_view TypeName() noexcept {
    return "aster.resource.Spi/v1";
  }
  LibXR::SPI& get() const noexcept { return spi_; }

 private:
  LibXR::SPI& spi_;
};

class GpioResource {
 public:
  explicit GpioResource(LibXR::GPIO& gpio) noexcept : gpio_(gpio) {}

  static constexpr std::string_view TypeName() noexcept {
    return "aster.resource.Gpio/v1";
  }
  LibXR::GPIO& get() const noexcept { return gpio_; }

 private:
  LibXR::GPIO& gpio_;
};

class PwmResource {
 public:
  explicit PwmResource(LibXR::PWM& pwm) noexcept : pwm_(pwm) {}

  static constexpr std::string_view TypeName() noexcept {
    return "aster.resource.Pwm/v1";
  }
  LibXR::PWM& get() const noexcept { return pwm_; }

 private:
  LibXR::PWM& pwm_;
};

class ByteStreamEndpoint {
 public:
  virtual ~ByteStreamEndpoint() = default;

  static constexpr std::string_view TypeName() noexcept {
    return "aster.resource.ByteStream/v1";
  }

  virtual aster::runtime::Status Read(
      std::span<std::byte> output, std::size_t& bytes_read,
      std::uint64_t& completion_time_ns,
      const aster::runtime::ExecutionContext& caller) noexcept = 0;
  virtual aster::runtime::Status Write(
      std::span<const std::byte> input,
      const aster::runtime::ExecutionContext& caller) noexcept = 0;
  virtual aster::runtime::Status Poll(
      const aster::runtime::ExecutionContext& caller) noexcept = 0;
};

}  // namespace aster::backend::libxr
