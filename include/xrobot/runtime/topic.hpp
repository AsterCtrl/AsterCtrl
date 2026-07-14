#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "xrobot/runtime/executor.hpp"
#include "xrobot/runtime/status.hpp"
#include "xrobot/runtime/type_support.hpp"

namespace xrobot::runtime {

enum class DeliveryPolicy : std::uint8_t {
  kLatest,
  kKeepAll,
};

struct MessageInfo {
  std::uint32_t sequence{};
  std::uint64_t source_timestamp_ns{};
};

struct SubscriptionStats {
  std::uint32_t received{};
  std::uint32_t delivered{};
  std::uint32_t dropped{};
  std::uint32_t overwritten{};
  std::uint32_t schedule_failures{};
  std::size_t high_watermark{};
};

struct TopicStats {
  std::uint32_t published{};
  std::uint32_t delivery_failures{};
};

template <MessageType Message>
using TopicCallback = void (*)(void*, const Message&, const MessageInfo&,
                               const ExecutionContext&) noexcept;

template <MessageType Message>
class TopicSink {
 public:
  virtual ~TopicSink() = default;

  virtual Status Deliver(const Message& message, const MessageInfo& info,
                         const ExecutionContext& caller) noexcept = 0;
};

template <MessageType Message>
class TopicSubscriberEndpoint {
 public:
  virtual ~TopicSubscriberEndpoint() = default;
  virtual Status Bind(TopicCallback<Message> callback,
                      void* callback_state) noexcept = 0;
};

template <MessageType Message>
class TopicSubscriber {
 public:
  constexpr TopicSubscriber() noexcept = default;
  constexpr explicit TopicSubscriber(
      TopicSubscriberEndpoint<Message>& endpoint) noexcept
      : endpoint_(&endpoint) {}

  Status Bind(TopicCallback<Message> callback,
              void* callback_state) const noexcept {
    if (endpoint_ == nullptr) {
      return Status::kUnavailable;
    }
    return endpoint_->Bind(callback, callback_state);
  }

 private:
  TopicSubscriberEndpoint<Message>* endpoint_{};
};

template <MessageType Message>
class TopicSource {
 public:
  virtual ~TopicSource() = default;

  virtual Status Publish(const Message& message,
                         std::uint64_t source_timestamp_ns,
                         const ExecutionContext& caller) noexcept = 0;
};

template <MessageType Message>
class TopicPublisher {
 public:
  constexpr TopicPublisher() noexcept = default;
  constexpr explicit TopicPublisher(TopicSource<Message>& source) noexcept
      : source_(&source) {}

  Status Publish(const Message& message, std::uint64_t source_timestamp_ns,
                 const ExecutionContext& caller) const noexcept {
    if (source_ == nullptr) {
      return Status::kUnavailable;
    }
    return source_->Publish(message, source_timestamp_ns, caller);
  }

  constexpr explicit operator bool() const noexcept {
    return source_ != nullptr;
  }

 private:
  TopicSource<Message>* source_{};
};

template <MessageType Message, std::size_t Depth>
class TopicSubscription final : public TopicSink<Message>,
                                public TopicSubscriberEndpoint<Message> {
 public:
  static_assert(Depth > 0);

  using Callback = TopicCallback<Message>;

  constexpr TopicSubscription(Executor& executor,
                              DeliveryPolicy policy) noexcept
      : executor_(executor), policy_(policy) {}

  constexpr TopicSubscription(Executor& executor, DeliveryPolicy policy,
                              Callback callback, void* callback_state) noexcept
      : executor_(executor),
        policy_(policy),
        callback_(callback),
        callback_state_(callback_state) {}

  Status Bind(Callback callback, void* callback_state) noexcept override {
    if (callback == nullptr) {
      return Status::kInvalidArgument;
    }
    if (callback_ != nullptr || size_ != 0 || stats_.received != 0) {
      return Status::kInvalidState;
    }
    callback_ = callback;
    callback_state_ = callback_state;
    return Status::kOk;
  }

  Status Deliver(const Message& message, const MessageInfo& info,
                 const ExecutionContext& caller) noexcept override {
    if (callback_ == nullptr) {
      return Status::kInvalidArgument;
    }

    ++stats_.received;
    if (policy_ == DeliveryPolicy::kLatest && size_ > 0) {
      queue_[head_] = Envelope{message, info};
      ++stats_.overwritten;
    } else if (size_ == Depth) {
      ++stats_.dropped;
      if (!scheduled_) {
        const auto status = Schedule(caller);
        if (!IsOk(status)) {
          DiscardPending();
          return status;
        }
      }
      return Status::kCapacityExceeded;
    } else {
      queue_[tail_] = Envelope{message, info};
      tail_ = (tail_ + 1) % Depth;
      ++size_;
      if (size_ > stats_.high_watermark) {
        stats_.high_watermark = size_;
      }
    }

    if (scheduled_) {
      return Status::kOk;
    }
    const auto status = Schedule(caller);
    if (!IsOk(status)) {
      DiscardPending();
    }
    return status;
  }

  const SubscriptionStats& stats() const noexcept { return stats_; }
  std::size_t pending() const noexcept { return size_; }

 private:
  struct Envelope {
    Message message{};
    MessageInfo info{};
  };

  static void DrainThunk(void* state,
                         const ExecutionContext& context) noexcept {
    static_cast<TopicSubscription*>(state)->Drain(context);
  }

  Status Schedule(const ExecutionContext& caller) noexcept {
    scheduled_ = true;
    const auto status = executor_.TryPost({DrainThunk, this}, caller);
    if (!IsOk(status)) {
      scheduled_ = false;
      ++stats_.schedule_failures;
    }
    return status;
  }

  void DiscardPending() noexcept {
    stats_.dropped += static_cast<std::uint32_t>(size_);
    head_ = 0;
    tail_ = 0;
    size_ = 0;
  }

  void Drain(const ExecutionContext& context) noexcept {
    if (size_ == 0) {
      scheduled_ = false;
      return;
    }

    const auto envelope = queue_[head_];
    head_ = (head_ + 1) % Depth;
    --size_;
    callback_(callback_state_, envelope.message, envelope.info, context);
    ++stats_.delivered;

    if (size_ == 0) {
      scheduled_ = false;
      return;
    }
    Schedule(context);
  }

  Executor& executor_;
  DeliveryPolicy policy_;
  Callback callback_{};
  void* callback_state_{};
  std::array<Envelope, Depth> queue_{};
  std::size_t head_{};
  std::size_t tail_{};
  std::size_t size_{};
  bool scheduled_{};
  SubscriptionStats stats_{};
};

template <MessageType Message, std::size_t MaxSubscriptions>
class StaticTopic final : public TopicSource<Message> {
 public:
  static_assert(MaxSubscriptions > 0);

  constexpr explicit StaticTopic(std::string_view name) noexcept : name_(name) {}

  Status Connect(TopicSink<Message>& sink) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < subscription_count_; ++index) {
      if (subscriptions_[index] == &sink) {
        return Status::kInvalidArgument;
      }
    }
    if (subscription_count_ == MaxSubscriptions) {
      return Status::kCapacityExceeded;
    }
    subscriptions_[subscription_count_++] = &sink;
    return Status::kOk;
  }

  Status Seal() noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (name_.empty()) {
      return Status::kInvalidArgument;
    }
    sealed_ = true;
    return Status::kOk;
  }

  Status Publish(const Message& message, std::uint64_t source_timestamp_ns,
                 const ExecutionContext& caller) noexcept override {
    if (!sealed_) {
      return Status::kInvalidState;
    }

    const MessageInfo info{++sequence_, source_timestamp_ns};
    ++stats_.published;
    Status result = Status::kOk;
    for (std::size_t index = 0; index < subscription_count_; ++index) {
      const auto status = subscriptions_[index]->Deliver(message, info, caller);
      if (!IsOk(status)) {
        ++stats_.delivery_failures;
        if (IsOk(result)) {
          result = status;
        }
      }
    }
    return result;
  }

  TopicPublisher<Message> publisher() noexcept {
    return TopicPublisher<Message>(*this);
  }
  std::string_view name() const noexcept { return name_; }
  std::size_t subscription_count() const noexcept {
    return subscription_count_;
  }
  const TopicStats& stats() const noexcept { return stats_; }

 private:
  std::string_view name_;
  std::array<TopicSink<Message>*, MaxSubscriptions> subscriptions_{};
  std::size_t subscription_count_{};
  std::uint32_t sequence_{};
  bool sealed_{};
  TopicStats stats_{};
};

}  // namespace xrobot::runtime
