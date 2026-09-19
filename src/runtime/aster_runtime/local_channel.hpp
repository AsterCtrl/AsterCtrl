#pragma once

#include "aster_runtime/core/channel.hpp"

namespace aster {

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

  Status RegisterSubscriber(const ChannelDescriptor& descriptor, aster_channel_callback_t callback,
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
      const auto abi_info = ToAbiMessageInfo(info);
      const auto abi_context = ToAbiExecutionContext(caller);
      const auto status = FromAbiStatus(subscriber.callback(
          subscriber.state, reinterpret_cast<const std::uint8_t*>(message.data()), message.size(),
          &abi_info, &abi_context));
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
    aster_channel_callback_t callback{};
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
