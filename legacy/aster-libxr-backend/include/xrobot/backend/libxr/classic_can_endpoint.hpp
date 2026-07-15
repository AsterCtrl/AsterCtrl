#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "can.hpp"
#include "xrobot/backend/libxr/error.hpp"
#include "xrobot/runtime/execution_context.hpp"
#include "xrobot/runtime/status.hpp"
#include "xrobot/transport/can/link.hpp"

namespace xrobot::backend::libxr {

struct ClassicCanFrame {
  std::uint16_t id{};
  std::uint8_t size{};
  std::array<std::byte, 8> data{};
};

struct ClassicCanReceiver {
  using Receive = void (*)(void*, const ClassicCanFrame&, std::uint64_t,
                           bool) noexcept;

  Receive receive{};
  void* state{};

  void Accept(const ClassicCanFrame& frame, std::uint64_t receive_time_ns,
              bool in_interrupt) const noexcept {
    if (receive != nullptr) {
      receive(state, frame, receive_time_ns, in_interrupt);
    }
  }
};

class ClassicCanEndpoint {
 public:
  virtual ~ClassicCanEndpoint() = default;

  static constexpr std::string_view TypeName() noexcept {
    return "xrobot.hardware.ClassicCanEndpoint/v1";
  }

  virtual xrobot::runtime::Status Subscribe(
      std::uint16_t first_id, std::uint16_t last_id,
      ClassicCanReceiver receiver) noexcept = 0;
  virtual xrobot::runtime::Status Write(
      const ClassicCanFrame& frame,
      const xrobot::runtime::ExecutionContext& caller) noexcept = 0;
};

struct ClassicCanEndpointStats {
  std::uint32_t rx_frames{};
  std::uint32_t rx_invalid{};
  std::uint32_t rx_deliveries{};
  std::uint32_t tx_frames{};
  std::uint32_t tx_failures{};
};

template <std::size_t SubscriptionCapacity>
class LibxrClassicCanEndpoint final : public ClassicCanEndpoint {
 public:
  static_assert(SubscriptionCapacity > 0);
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  LibxrClassicCanEndpoint(
      LibXR::CAN& can,
      xrobot::transport::can::CanClockReader clock) noexcept
      : can_(can), clock_(clock) {}

  LibxrClassicCanEndpoint(const LibxrClassicCanEndpoint&) = delete;
  LibxrClassicCanEndpoint& operator=(const LibxrClassicCanEndpoint&) = delete;

  xrobot::runtime::Status Subscribe(
      std::uint16_t first_id, std::uint16_t last_id,
      ClassicCanReceiver receiver) noexcept override {
    using xrobot::runtime::Status;

    if (initialized_) return Status::kInvalidState;
    if (receiver.receive == nullptr || first_id > last_id || last_id > 0x7ffU) {
      return Status::kInvalidArgument;
    }
    if (subscription_count_ == subscriptions_.size()) {
      return Status::kCapacityExceeded;
    }
    subscriptions_[subscription_count_++] = {first_id, last_id, receiver};
    return Status::kOk;
  }

  xrobot::runtime::Status Initialize() noexcept {
    using xrobot::runtime::Status;

    if (initialized_) return Status::kInvalidState;
    if (clock_.read == nullptr || subscription_count_ == 0U) {
      return Status::kInvalidArgument;
    }
    receive_callback_ = LibXR::CAN::Callback::Create(ReceiveThunk, this);
    can_.Register(receive_callback_, LibXR::CAN::Type::STANDARD,
                  LibXR::CAN::FilterMode::ID_RANGE, 0, 0x7ffU);
    initialized_ = true;
    return Status::kOk;
  }

  xrobot::runtime::Status Write(
      const ClassicCanFrame& frame,
      const xrobot::runtime::ExecutionContext& caller) noexcept override {
    using xrobot::runtime::ExecutionKind;
    using xrobot::runtime::Status;

    if (!initialized_) return Status::kInvalidState;
    if (caller.kind() == ExecutionKind::kInterrupt || frame.id > 0x7ffU ||
        frame.size == 0U || frame.size > frame.data.size()) {
      return Status::kInvalidArgument;
    }

    LibXR::CAN::ClassicPack pack{};
    pack.id = frame.id;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = frame.size;
    for (std::size_t index = 0; index < frame.size; ++index) {
      pack.data[index] = std::to_integer<std::uint8_t>(frame.data[index]);
    }
    const auto status = MapError(can_.AddMessage(pack));
    if (status == Status::kOk) {
      tx_frames_.fetch_add(1, std::memory_order_relaxed);
    } else {
      tx_failures_.fetch_add(1, std::memory_order_relaxed);
    }
    return status;
  }

  ClassicCanEndpointStats stats() const noexcept {
    return {
        rx_frames_.load(std::memory_order_relaxed),
        rx_invalid_.load(std::memory_order_relaxed),
        rx_deliveries_.load(std::memory_order_relaxed),
        tx_frames_.load(std::memory_order_relaxed),
        tx_failures_.load(std::memory_order_relaxed),
    };
  }

 private:
  struct Subscription {
    std::uint16_t first_id{};
    std::uint16_t last_id{};
    ClassicCanReceiver receiver{};
  };

  static void ReceiveThunk(bool in_interrupt,
                           LibxrClassicCanEndpoint* self,
                           const LibXR::CAN::ClassicPack& pack) noexcept {
    self->Receive(pack, in_interrupt);
  }

  void Receive(const LibXR::CAN::ClassicPack& pack,
               bool in_interrupt) noexcept {
    if (pack.type != LibXR::CAN::Type::STANDARD || pack.id > 0x7ffU ||
        pack.dlc == 0U || pack.dlc > 8U) {
      rx_invalid_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    ClassicCanFrame frame{};
    frame.id = static_cast<std::uint16_t>(pack.id);
    frame.size = pack.dlc;
    for (std::size_t index = 0; index < pack.dlc; ++index) {
      frame.data[index] = static_cast<std::byte>(pack.data[index]);
    }
    const auto receive_time_ns = clock_.NowNs();
    rx_frames_.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t index = 0; index < subscription_count_; ++index) {
      const auto& subscription = subscriptions_[index];
      if (frame.id < subscription.first_id || frame.id > subscription.last_id) {
        continue;
      }
      subscription.receiver.Accept(frame, receive_time_ns, in_interrupt);
      rx_deliveries_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  LibXR::CAN& can_;
  xrobot::transport::can::CanClockReader clock_;
  std::array<Subscription, SubscriptionCapacity> subscriptions_{};
  std::size_t subscription_count_{};
  LibXR::CAN::Callback receive_callback_{};
  bool initialized_{};
  std::atomic<std::uint32_t> rx_frames_{};
  std::atomic<std::uint32_t> rx_invalid_{};
  std::atomic<std::uint32_t> rx_deliveries_{};
  std::atomic<std::uint32_t> tx_frames_{};
  std::atomic<std::uint32_t> tx_failures_{};
};

}  // namespace xrobot::backend::libxr
