#include "aster_runtime/platform/linux/channel_manager.hpp"

#include <atomic>
#include <list>
#include <mutex>

#include "name_resolution.hpp"

namespace aster::platform::linux {

struct ChannelManager::Impl {
  struct Subscription {
    aster_channel_callback_t callback;
    void* state;
    ExecutorRef executor;
  };
  struct Topic {
    std::string type_name;
    TypeDescriptor type;
    std::vector<Subscription> subscribers;
    std::uint32_t sequence{};
    std::size_t publishers{};
  };
  struct Delivery {
    Impl* owner;
    Subscription subscriber;
    std::shared_ptr<const std::vector<std::byte>> payload;
    aster_message_info_t info;
    std::list<Delivery>::iterator position;
  };

  class Endpoint final : public ChannelBackend {
   public:
    Endpoint(Impl& owner, const ModuleConfig& config, ExecutorRef executor)
        : owner_(owner), config_(config), executor_(executor) {}

    Status RegisterPublisher(const ChannelDescriptor& descriptor) noexcept override {
      return Register(descriptor, nullptr, nullptr);
    }
    Status RegisterSubscriber(const ChannelDescriptor& descriptor,
                              aster_channel_callback_t callback, void* state) noexcept override {
      return callback == nullptr ? Status::kInvalidArgument : Register(descriptor, callback, state);
    }
    Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                   std::uint64_t timestamp, const ExecutionContext& caller) noexcept override {
      try {
        const std::lock_guard lock(owner_.mutex);
        if (!owner_.sealed || !owner_.accepting) return Status::kInvalidState;
        const auto binding = bindings_.find(descriptor.name);
        if (binding == bindings_.end() || !binding->second.publisher) return Status::kNotFound;
        auto& topic = *binding->second.topic;
        if (!SameType(topic.type, descriptor.message_type)) return Status::kTypeMismatch;
        if (message.size() > topic.type.max_serialized_size) return Status::kCapacityExceeded;
        if (topic.subscribers.empty()) return Status::kOk;
        auto payload =
            std::make_shared<const std::vector<std::byte>>(message.begin(), message.end());
        topic.sequence = topic.sequence == UINT32_MAX ? 1 : topic.sequence + 1;
        const aster_message_info_t info{topic.sequence, timestamp};
        Status result = Status::kOk;
        for (const auto& subscriber : topic.subscribers) {
          auto position = owner_.pending.emplace(owner_.pending.end(),
                                                 Delivery{&owner_, subscriber, payload, info, {}});
          position->position = position;
          const auto status =
              subscriber.executor.TryPost(WorkItem::Bind<Deliver>(&*position), caller);
          if (!IsOk(status)) {
            owner_.pending.erase(position);
            if (IsOk(result)) result = status;
          }
        }
        // Broadcast acceptance is per subscriber; a full destination does not
        // retract work already accepted by another executor.
        return result;
      } catch (const std::bad_alloc&) {
        return Status::kCapacityExceeded;
      } catch (...) {
        return Status::kInternal;
      }
    }
    Status Seal() noexcept override {
      return Status::kInvalidState;
    }  // Manager seals all endpoints.
    bool sealed() const noexcept override { return owner_.sealed; }

   private:
    struct Binding {
      Topic* topic{};
      bool publisher{};
    };

    Status Register(const ChannelDescriptor& descriptor, aster_channel_callback_t callback,
                    void* state) noexcept {
      try {
        const std::lock_guard lock(owner_.mutex);
        if (owner_.sealed) return Status::kInvalidState;
        if (!executor_ || descriptor.message_type.name.empty() ||
            descriptor.message_type.max_serialized_size == 0)
          return Status::kInvalidArgument;
        std::string name;
        const auto status = ResolveInstanceName(config_, descriptor.name, name);
        if (!IsOk(status)) return status;
        auto found = owner_.topics.find(name);
        if (found == owner_.topics.end()) {
          auto topic = std::make_unique<Topic>();
          topic->type_name = descriptor.message_type.name;
          topic->type = descriptor.message_type;
          topic->type.name = topic->type_name;
          found = owner_.topics.emplace(std::move(name), std::move(topic)).first;
        }
        auto& topic = *found->second;
        if (!SameType(topic.type, descriptor.message_type)) return Status::kTypeMismatch;
        auto& binding = bindings_[std::string(descriptor.name)];
        binding.topic = &topic;
        if (callback != nullptr)
          topic.subscribers.push_back({callback, state, executor_});
        else if (!binding.publisher) {
          binding.publisher = true;
          ++topic.publishers;
        }
        return Status::kOk;
      } catch (const std::bad_alloc&) {
        return Status::kCapacityExceeded;
      } catch (...) {
        return Status::kInternal;
      }
    }

    Impl& owner_;
    const ModuleConfig& config_;
    ExecutorRef executor_;
    std::map<std::string, Binding, std::less<>> bindings_;
  };

  static void Deliver(void* state, const ExecutionContext& context) noexcept {
    auto& delivery = *static_cast<Delivery*>(state);
    auto& owner = *delivery.owner;
    const auto native_context = ToAbiExecutionContext(context);
    Status status;
    try {
      status = FromAbiStatus(delivery.subscriber.callback(
          delivery.subscriber.state,
          reinterpret_cast<const std::uint8_t*>(delivery.payload->data()), delivery.payload->size(),
          &delivery.info, &native_context));
    } catch (...) {
      status = Status::kInternal;
    }
    const std::lock_guard lock(owner.mutex);
    if (!IsOk(status)) owner.last_failure = status;
    owner.pending.erase(delivery.position);
  }

  std::mutex mutex;
  std::map<std::string, std::unique_ptr<Topic>, std::less<>> topics;
  std::list<Delivery> pending;
  std::atomic<bool> sealed{};
  bool accepting{};
  Status last_failure{Status::kOk};
};

ChannelManager::ChannelManager() : impl_(std::make_unique<Impl>()) {}
ChannelManager::~ChannelManager() = default;
std::unique_ptr<ChannelBackend> ChannelManager::CreateEndpoint(const ModuleConfig& config,
                                                               ExecutorRef executor) {
  return std::make_unique<Impl::Endpoint>(*impl_, config, executor);
}
Status ChannelManager::Seal() noexcept {
  const std::lock_guard lock(impl_->mutex);
  if (impl_->sealed) return Status::kInvalidState;
  for (const auto& [name, topic] : impl_->topics) {
    if (!topic->subscribers.empty() && topic->publishers == 0) return Status::kUnavailable;
  }
  impl_->sealed = true;
  impl_->accepting = true;
  return Status::kOk;
}
bool ChannelManager::sealed() const noexcept { return impl_->sealed; }
void ChannelManager::Close() noexcept {
  const std::lock_guard lock(impl_->mutex);
  impl_->accepting = false;
}
void ChannelManager::ClearPending() noexcept {
  const std::lock_guard lock(impl_->mutex);
  impl_->pending.clear();
}

}  // namespace aster::platform::linux
