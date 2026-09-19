#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster_module_c_interface/channel/channel_base.h"
#include "aster_module_cpp_interface/execution.hpp"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/type_support.hpp"

namespace aster {

struct ChannelDescriptor {
  std::string_view name;
  TypeDescriptor message_type;
};

struct MessageInfo {
  std::uint32_t sequence{};
  std::uint64_t source_timestamp_ns{};
};

[[nodiscard]] constexpr aster_channel_descriptor_t ToAbiChannelDescriptor(
    const ChannelDescriptor& descriptor) noexcept {
  return {
      ToAbiString(descriptor.name),
      ToAbiTypeDescriptor(descriptor.message_type),
  };
}

inline Status FromAbiChannelDescriptor(const aster_channel_descriptor_t* descriptor,
                                       ChannelDescriptor& result) noexcept {
  if (descriptor == nullptr) {
    return Status::kInvalidArgument;
  }
  if (descriptor->name.data == nullptr || descriptor->name.size == 0) {
    return Status::kInvalidArgument;
  }
  TypeDescriptor message_type{};
  const auto status = FromAbiTypeDescriptor(&descriptor->message_type, message_type);
  if (!IsOk(status)) {
    return status;
  }
  result = {FromAbiString(descriptor->name), message_type};
  return Status::kOk;
}

[[nodiscard]] constexpr aster_message_info_t ToAbiMessageInfo(const MessageInfo& info) noexcept {
  return {info.sequence, info.source_timestamp_ns};
}

using RawChannelCallback = Status (*)(void*, std::span<const std::byte>, const MessageInfo&,
                                      const ExecutionContext&) noexcept;

class ChannelRef {
 public:
  constexpr ChannelRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_channel_base_t*>;
    }
  explicit ChannelRef(Backend& value) noexcept : ChannelRef(value.NativeHandle()) {}
  constexpr explicit ChannelRef(const aster_channel_base_t* backend) noexcept
      : abi_(backend != nullptr && backend->struct_size >= sizeof(*backend) ? backend : nullptr) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  Status RegisterPublisher(const ChannelDescriptor& descriptor) const noexcept {
    if (abi_ == nullptr || abi_->register_publisher == nullptr) {
      return Status::kUnavailable;
    }
    const auto native = ToAbiChannelDescriptor(descriptor);
    return FromAbiStatus(abi_->register_publisher(abi_->impl, &native));
  }

  Status RegisterSubscriberAbi(const ChannelDescriptor& descriptor,
                               aster_channel_callback_t callback,
                               void* callback_state) const noexcept {
    if (abi_ == nullptr || abi_->register_subscriber == nullptr) {
      return Status::kUnavailable;
    }
    const auto native = ToAbiChannelDescriptor(descriptor);
    return FromAbiStatus(abi_->register_subscriber(abi_->impl, &native, callback, callback_state));
  }

  template <RawChannelCallback Callback>
  Status RegisterSubscriber(const ChannelDescriptor& descriptor,
                            void* callback_state) const noexcept {
    return RegisterSubscriberAbi(descriptor, Invoke<Callback>, callback_state);
  }

  Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                 std::uint64_t timestamp, const ExecutionContext& caller) const noexcept {
    return Publish(descriptor, message, timestamp, &caller);
  }

  Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                 std::uint64_t source_timestamp_ns = 0,
                 const ExecutionContext* caller = nullptr) const noexcept {
    if (abi_ == nullptr || abi_->publish == nullptr) {
      return Status::kUnavailable;
    }
    const auto native = ToAbiChannelDescriptor(descriptor);
    const auto context =
        caller == nullptr ? aster_execution_context_t{} : ToAbiExecutionContext(*caller);
    return FromAbiStatus(
        abi_->publish(abi_->impl, &native, reinterpret_cast<const std::uint8_t*>(message.data()),
                      message.size(), source_timestamp_ns, caller == nullptr ? nullptr : &context));
  }

  [[nodiscard]] constexpr const aster_channel_base_t* NativeHandle() const noexcept { return abi_; }

 private:
  template <RawChannelCallback Callback>
  static aster_status_t Invoke(void* state, const std::uint8_t* message, std::size_t message_size,
                               const aster_message_info_t* info,
                               const aster_execution_context_t* context) noexcept {
    if (message == nullptr && message_size != 0) {
      return ASTER_STATUS_INVALID_ARGUMENT;
    }
    if (info == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT;
    }
    const auto context_status = ValidateAbiExecutionContext(context);
    if (!IsOk(context_status)) {
      return ToAbiStatus(context_status);
    }
    const MessageInfo native_info{info->sequence, info->source_timestamp_ns};
    const auto native_context = FromAbiExecutionContext(*context);
    return ToAbiStatus(Callback(state, {reinterpret_cast<const std::byte*>(message), message_size},
                                native_info, native_context));
  }

  const aster_channel_base_t* abi_{};
};

template <MessageType Message>
class Publisher {
 public:
  Status Bind(ChannelRef channel, std::string_view name) noexcept {
    const ChannelDescriptor candidate{name, TypeSupport<Message>::descriptor()};
    const auto status = channel.RegisterPublisher(candidate);
    if (!IsOk(status)) {
      return status;
    }
    channel_ = channel;
    descriptor_ = candidate;
    return Status::kOk;
  }

  Status Publish(const Message& message, std::uint64_t timestamp,
                 const ExecutionContext& caller) const noexcept {
    return Publish(message, timestamp, &caller);
  }

  Status Publish(const Message& message, std::uint64_t source_timestamp_ns = 0,
                 const ExecutionContext* caller = nullptr) const noexcept {
    constexpr auto capacity = TypeSupport<Message>::descriptor().max_serialized_size;
    static_assert(capacity > 0);
    std::array<std::byte, capacity> encoded{};
    std::size_t written{};
    const auto status = TypeSupport<Message>::Encode(message, encoded, written);
    if (!IsOk(status)) {
      return status;
    }
    if (written > encoded.size()) {
      return Status::kInternal;
    }
    return channel_.Publish(descriptor_, std::span<const std::byte>(encoded.data(), written),
                            source_timestamp_ns, caller);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return static_cast<bool>(channel_);
  }

 private:
  ChannelRef channel_;
  ChannelDescriptor descriptor_{};
};

template <MessageType Message>
class Subscriber {
 public:
  using Callback = Status (*)(void*, const Message&, const MessageInfo&,
                              const ExecutionContext&) noexcept;

  Status Bind(ChannelRef channel, std::string_view name, Callback callback,
              void* callback_state) noexcept {
    if (callback == nullptr || bound_) {
      return Status::kInvalidArgument;
    }
    callback_ = callback;
    callback_state_ = callback_state;
    const ChannelDescriptor descriptor{name, TypeSupport<Message>::descriptor()};
    const auto status = channel.RegisterSubscriber<Dispatch>(descriptor, this);
    if (!IsOk(status)) {
      callback_ = nullptr;
      callback_state_ = nullptr;
      return status;
    }
    bound_ = true;
    return Status::kOk;
  }

  [[nodiscard]] constexpr bool bound() const noexcept { return bound_; }

 private:
  static Status Dispatch(void* state, std::span<const std::byte> encoded, const MessageInfo& info,
                         const ExecutionContext& caller) noexcept {
    auto& self = *static_cast<Subscriber*>(state);
    Message message{};
    const auto status = TypeSupport<Message>::Decode(encoded, message);
    return IsOk(status) ? self.callback_(self.callback_state_, message, info, caller) : status;
  }

  Callback callback_{};
  void* callback_state_{};
  bool bound_{};
};

}  // namespace aster
