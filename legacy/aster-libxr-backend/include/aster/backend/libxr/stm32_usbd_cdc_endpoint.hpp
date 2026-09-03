#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/backend/libxr/resources.hpp"
#include "aster/runtime/runtime_services.hpp"

namespace aster::backend::libxr {

struct Stm32UsbdCdcDriver {
  using Callback = void (*)(std::uint16_t);
  using RegisterCallbacks = std::uint8_t* (*)(Callback, Callback);
  using Transmit = std::uint8_t (*)(std::uint8_t*, std::uint16_t);

  RegisterCallbacks register_callbacks{};
  Transmit transmit{};
  std::size_t receive_buffer_size{};
  std::uint8_t success_code{};
  std::uint8_t busy_code{1U};
};

struct Stm32UsbdCdcEndpointStats {
  std::uint32_t rx_chunks{};
  std::uint32_t rx_bytes{};
  std::uint32_t rx_dropped{};
  std::uint32_t rx_invalid{};
  std::uint32_t tx_queued{};
  std::uint32_t tx_completed{};
  std::uint32_t tx_dropped{};
  std::uint32_t tx_failures{};
  std::uint32_t tx_invalid_completions{};
};

template <std::size_t MaximumChunkBytes, std::size_t ReceiveQueueCapacity,
          std::size_t TransmitQueueCapacity>
class Stm32UsbdCdcEndpoint final : public ByteStreamEndpoint {
 public:
  static_assert(MaximumChunkBytes > 0U && MaximumChunkBytes <= 512U);
  static_assert(ReceiveQueueCapacity > 0U);
  static_assert(TransmitQueueCapacity > 0U);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  Stm32UsbdCdcEndpoint(Stm32UsbdCdcDriver driver,
                       aster::runtime::SteadyClock& clock) noexcept
      : driver_(driver), clock_(clock) {}

  aster::runtime::Status Initialize() noexcept {
    using aster::runtime::Status;
    if (initialized_ || active_ != nullptr) return Status::kInvalidState;
    if (driver_.register_callbacks == nullptr || driver_.transmit == nullptr ||
        driver_.receive_buffer_size < MaximumChunkBytes ||
        driver_.success_code == driver_.busy_code) {
      return Status::kInvalidArgument;
    }
    active_ = this;
    receive_buffer_ =
        driver_.register_callbacks(TransmitCompleteThunk, ReceiveThunk);
    if (receive_buffer_ == nullptr) {
      active_ = nullptr;
      return Status::kInvalidState;
    }
    initialized_ = true;
    return Status::kOk;
  }

  aster::runtime::Status Read(
      std::span<std::byte> output, std::size_t& bytes_read,
      std::uint64_t& completion_time_ns,
      const aster::runtime::ExecutionContext& caller) noexcept override {
    using aster::runtime::ExecutionKind;
    using aster::runtime::Status;
    bytes_read = 0U;
    completion_time_ns = 0U;
    if (!initialized_) return Status::kInvalidState;
    if (output.empty() || caller.kind() == ExecutionKind::kInterrupt) {
      return Status::kInvalidArgument;
    }

    const std::uint32_t head = rx_head_.load(std::memory_order_relaxed);
    if (head == rx_tail_.load(std::memory_order_acquire)) {
      return Status::kUnavailable;
    }
    const auto& chunk = receive_queue_[head];
    if (output.size() < chunk.size) return Status::kCapacityExceeded;
    for (std::size_t index = 0U; index < chunk.size; ++index) {
      output[index] = chunk.data[index];
    }
    bytes_read = chunk.size;
    completion_time_ns = chunk.completion_time_ns;
    rx_head_.store(Increment<ReceiveQueueCapacity>(head),
                   std::memory_order_release);
    return Status::kOk;
  }

  aster::runtime::Status Write(
      std::span<const std::byte> input,
      const aster::runtime::ExecutionContext& caller) noexcept override {
    using aster::runtime::ExecutionKind;
    using aster::runtime::Status;
    if (!initialized_) return Status::kInvalidState;
    if (input.empty() || input.size() > MaximumChunkBytes ||
        caller.kind() == ExecutionKind::kInterrupt) {
      return Status::kInvalidArgument;
    }

    const std::uint32_t tail = tx_tail_.load(std::memory_order_relaxed);
    const std::uint32_t next = Increment<TransmitQueueCapacity>(tail);
    if (next == tx_head_.load(std::memory_order_acquire)) {
      tx_dropped_.fetch_add(1U, std::memory_order_relaxed);
      return Status::kCapacityExceeded;
    }
    auto& chunk = transmit_queue_[tail];
    for (std::size_t index = 0U; index < input.size(); ++index) {
      chunk.data[index] = input[index];
    }
    chunk.size = static_cast<std::uint16_t>(input.size());
    tx_tail_.store(next, std::memory_order_release);
    tx_queued_.fetch_add(1U, std::memory_order_relaxed);
    return Status::kOk;
  }

  aster::runtime::Status Poll(
      const aster::runtime::ExecutionContext& caller) noexcept override {
    using aster::runtime::ExecutionKind;
    using aster::runtime::Status;
    if (!initialized_) return Status::kInvalidState;
    if (caller.kind() != ExecutionKind::kThread) {
      return Status::kInvalidArgument;
    }
    if (tx_active_.load(std::memory_order_acquire)) {
      return Status::kUnavailable;
    }

    const std::uint32_t head = tx_head_.load(std::memory_order_relaxed);
    if (head == tx_tail_.load(std::memory_order_acquire)) {
      return Status::kUnavailable;
    }
    auto& chunk = transmit_queue_[head];
    tx_active_.store(true, std::memory_order_release);
    const std::uint8_t result = driver_.transmit(
        reinterpret_cast<std::uint8_t*>(chunk.data.data()), chunk.size);
    if (result == driver_.success_code) return Status::kOk;
    tx_active_.store(false, std::memory_order_release);
    if (result == driver_.busy_code) return Status::kUnavailable;
    tx_failures_.fetch_add(1U, std::memory_order_relaxed);
    return Status::kInternal;
  }

  Stm32UsbdCdcEndpointStats stats() const noexcept {
    return {
        rx_chunks_.load(std::memory_order_relaxed),
        rx_bytes_.load(std::memory_order_relaxed),
        rx_dropped_.load(std::memory_order_relaxed),
        rx_invalid_.load(std::memory_order_relaxed),
        tx_queued_.load(std::memory_order_relaxed),
        tx_completed_.load(std::memory_order_relaxed),
        tx_dropped_.load(std::memory_order_relaxed),
        tx_failures_.load(std::memory_order_relaxed),
        tx_invalid_completions_.load(std::memory_order_relaxed),
    };
  }

 private:
  struct Chunk {
    std::array<std::byte, MaximumChunkBytes> data{};
    std::uint64_t completion_time_ns{};
    std::uint16_t size{};
  };

  template <std::size_t Capacity>
  static constexpr std::uint32_t Increment(std::uint32_t index) noexcept {
    return (index + 1U) % static_cast<std::uint32_t>(Capacity + 1U);
  }

  static void ReceiveThunk(std::uint16_t size) noexcept {
    if (active_ != nullptr) active_->Receive(size);
  }

  static void TransmitCompleteThunk(std::uint16_t size) noexcept {
    if (active_ != nullptr) active_->TransmitComplete(size);
  }

  void Receive(std::uint16_t size) noexcept {
    if (!initialized_ || size == 0U || size > MaximumChunkBytes ||
        size > driver_.receive_buffer_size) {
      rx_invalid_.fetch_add(1U, std::memory_order_relaxed);
      return;
    }
    const std::uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
    const std::uint32_t next = Increment<ReceiveQueueCapacity>(tail);
    if (next == rx_head_.load(std::memory_order_acquire)) {
      rx_dropped_.fetch_add(1U, std::memory_order_relaxed);
      return;
    }
    auto& chunk = receive_queue_[tail];
    for (std::size_t index = 0U; index < size; ++index) {
      chunk.data[index] = static_cast<std::byte>(receive_buffer_[index]);
    }
    chunk.size = size;
    chunk.completion_time_ns = clock_.NowNs();
    rx_tail_.store(next, std::memory_order_release);
    rx_chunks_.fetch_add(1U, std::memory_order_relaxed);
    rx_bytes_.fetch_add(size, std::memory_order_relaxed);
  }

  void TransmitComplete(std::uint16_t size) noexcept {
    if (!initialized_ ||
        !tx_active_.exchange(false, std::memory_order_acq_rel)) {
      tx_invalid_completions_.fetch_add(1U, std::memory_order_relaxed);
      return;
    }
    const std::uint32_t head = tx_head_.load(std::memory_order_relaxed);
    if (head == tx_tail_.load(std::memory_order_acquire) ||
        transmit_queue_[head].size != size) {
      tx_invalid_completions_.fetch_add(1U, std::memory_order_relaxed);
    }
    tx_head_.store(Increment<TransmitQueueCapacity>(head),
                   std::memory_order_release);
    tx_completed_.fetch_add(1U, std::memory_order_relaxed);
  }

  inline static Stm32UsbdCdcEndpoint* active_{};

  Stm32UsbdCdcDriver driver_;
  aster::runtime::SteadyClock& clock_;
  std::array<Chunk, ReceiveQueueCapacity + 1U> receive_queue_{};
  std::array<Chunk, TransmitQueueCapacity + 1U> transmit_queue_{};
  std::atomic<std::uint32_t> rx_head_{};
  std::atomic<std::uint32_t> rx_tail_{};
  std::atomic<std::uint32_t> tx_head_{};
  std::atomic<std::uint32_t> tx_tail_{};
  std::atomic<bool> tx_active_{};
  std::atomic<std::uint32_t> rx_chunks_{};
  std::atomic<std::uint32_t> rx_bytes_{};
  std::atomic<std::uint32_t> rx_dropped_{};
  std::atomic<std::uint32_t> rx_invalid_{};
  std::atomic<std::uint32_t> tx_queued_{};
  std::atomic<std::uint32_t> tx_completed_{};
  std::atomic<std::uint32_t> tx_dropped_{};
  std::atomic<std::uint32_t> tx_failures_{};
  std::atomic<std::uint32_t> tx_invalid_completions_{};
  std::uint8_t* receive_buffer_{};
  bool initialized_{};
};

}  // namespace aster::backend::libxr
