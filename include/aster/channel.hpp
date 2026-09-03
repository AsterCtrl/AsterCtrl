#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "aster/execution.hpp"
#include "aster/registry.hpp"
#include "aster/status.hpp"
#include "aster/type_support.hpp"

namespace aster {

struct ChannelDescriptor {
  std::string_view name;
  TypeDescriptor message_type;
};

struct MessageInfo {
  std::uint32_t sequence{};
  std::uint64_t source_timestamp_ns{};
};

using RawChannelCallback = Status (*)(void*, std::span<const std::byte>, const MessageInfo&,
                                      const ExecutionContext&) noexcept;

class ChannelBackend : public Registry {
 public:
  virtual Status RegisterPublisher(const ChannelDescriptor& descriptor) noexcept = 0;
  virtual Status RegisterSubscriber(const ChannelDescriptor& descriptor,
                                    RawChannelCallback callback, void* callback_state) noexcept = 0;
  virtual Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                         std::uint64_t source_timestamp_ns,
                         const ExecutionContext& caller) noexcept = 0;
};

class ChannelRef {
 public:
  constexpr ChannelRef() noexcept = default;
  constexpr explicit ChannelRef(ChannelBackend& backend) noexcept : backend_(&backend) {}

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return backend_ != nullptr; }

  Status RegisterPublisher(const ChannelDescriptor& descriptor) const noexcept {
    return backend_ == nullptr ? Status::kUnavailable : backend_->RegisterPublisher(descriptor);
  }

  Status RegisterSubscriber(const ChannelDescriptor& descriptor, RawChannelCallback callback,
                            void* callback_state) const noexcept {
    return backend_ == nullptr ? Status::kUnavailable
                               : backend_->RegisterSubscriber(descriptor, callback, callback_state);
  }

  Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                 std::uint64_t source_timestamp_ns, const ExecutionContext& caller) const noexcept {
    return backend_ == nullptr
               ? Status::kUnavailable
               : backend_->Publish(descriptor, message, source_timestamp_ns, caller);
  }

 private:
  ChannelBackend* backend_{};
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

  Status Publish(const Message& message, std::uint64_t source_timestamp_ns,
                 const ExecutionContext& caller) const noexcept {
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
    const auto status = channel.RegisterSubscriber(descriptor, Dispatch, this);
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

struct LocalChannelStats {
  std::uint32_t publications{};
  std::uint32_t deliveries{};
  std::uint32_t delivery_failures{};
};

template <std::size_t MaxTopics, std::size_t MaxSubscribersPerTopic, std::size_t MaximumMessageSize>
class LocalChannel final : public ChannelBackend {
 public:
  static_assert(MaxTopics > 0);
  static_assert(MaxSubscribersPerTopic > 0);
  static_assert(MaximumMessageSize > 0);

  Status RegisterPublisher(const ChannelDescriptor& descriptor) noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    Topic* topic{};
    const auto status = FindOrAdd(descriptor, topic);
    if (!IsOk(status)) {
      return status;
    }
    if (topic->publisher_count == UINT16_MAX) {
      return Status::kCapacityExceeded;
    }
    ++topic->publisher_count;
    return Status::kOk;
  }

  Status RegisterSubscriber(const ChannelDescriptor& descriptor, RawChannelCallback callback,
                            void* callback_state) noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (callback == nullptr) {
      return Status::kInvalidArgument;
    }
    Topic* topic{};
    const auto status = FindOrAdd(descriptor, topic);
    if (!IsOk(status)) {
      return status;
    }
    if (topic->subscriber_count == topic->subscribers.size()) {
      return Status::kCapacityExceeded;
    }
    topic->subscribers[topic->subscriber_count++] = {callback, callback_state};
    return Status::kOk;
  }

  Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                 std::uint64_t source_timestamp_ns,
                 const ExecutionContext& caller) noexcept override {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    auto* topic = Find(descriptor.name);
    if (topic == nullptr) {
      return Status::kNotFound;
    }
    if (!SameType(topic->descriptor.message_type, descriptor.message_type) ||
        message.size() > descriptor.message_type.max_serialized_size ||
        message.size() > MaximumMessageSize) {
      return Status::kTypeMismatch;
    }
    const MessageInfo info{NextSequence(*topic), source_timestamp_ns};
    Status first_failure{Status::kOk};
    publications_.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t index = 0; index < topic->subscriber_count; ++index) {
      const auto& subscriber = topic->subscribers[index];
      const auto status = subscriber.callback(subscriber.state, message, info, caller);
      if (IsOk(status)) {
        deliveries_.fetch_add(1, std::memory_order_relaxed);
      } else {
        delivery_failures_.fetch_add(1, std::memory_order_relaxed);
        if (IsOk(first_failure)) {
          first_failure = status;
        }
      }
    }
    return first_failure;
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < topic_count_; ++index) {
      if (topics_[index].subscriber_count != 0 && topics_[index].publisher_count == 0) {
        return Status::kUnavailable;
      }
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }
  [[nodiscard]] std::size_t topic_count() const noexcept { return topic_count_; }
  [[nodiscard]] LocalChannelStats stats() const noexcept {
    return {
        publications_.load(std::memory_order_relaxed),
        deliveries_.load(std::memory_order_relaxed),
        delivery_failures_.load(std::memory_order_relaxed),
    };
  }

 private:
  struct Subscription {
    RawChannelCallback callback{};
    void* state{};
  };

  struct Topic {
    ChannelDescriptor descriptor{};
    std::array<Subscription, MaxSubscribersPerTopic> subscribers{};
    std::size_t subscriber_count{};
    std::atomic<std::uint32_t> sequence{};
    std::uint16_t publisher_count{};
  };

  [[nodiscard]] static std::uint32_t NextSequence(Topic& topic) noexcept {
    auto current = topic.sequence.load(std::memory_order_relaxed);
    while (true) {
      const auto next = current == UINT32_MAX ? 1U : current + 1U;
      if (topic.sequence.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
        return next;
      }
    }
  }

  [[nodiscard]] Topic* Find(std::string_view name) noexcept {
    for (std::size_t index = 0; index < topic_count_; ++index) {
      if (topics_[index].descriptor.name == name) {
        return &topics_[index];
      }
    }
    return nullptr;
  }

  Status FindOrAdd(const ChannelDescriptor& descriptor, Topic*& topic) noexcept {
    topic = nullptr;
    if (descriptor.name.empty() || descriptor.message_type.name.empty() ||
        descriptor.message_type.max_serialized_size == 0 ||
        descriptor.message_type.max_serialized_size > MaximumMessageSize) {
      return Status::kInvalidArgument;
    }
    topic = Find(descriptor.name);
    if (topic != nullptr) {
      return SameType(topic->descriptor.message_type, descriptor.message_type)
                 ? Status::kOk
                 : Status::kTypeMismatch;
    }
    if (topic_count_ == topics_.size()) {
      return Status::kCapacityExceeded;
    }
    topic = &topics_[topic_count_++];
    topic->descriptor = descriptor;
    return Status::kOk;
  }

  std::array<Topic, MaxTopics> topics_{};
  std::size_t topic_count_{};
  std::atomic<std::uint32_t> publications_{};
  std::atomic<std::uint32_t> deliveries_{};
  std::atomic<std::uint32_t> delivery_failures_{};
  bool sealed_{};
};

}  // namespace aster
