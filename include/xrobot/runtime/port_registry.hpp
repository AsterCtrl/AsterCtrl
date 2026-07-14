#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "xrobot/runtime/action.hpp"
#include "xrobot/runtime/service.hpp"
#include "xrobot/runtime/status.hpp"
#include "xrobot/runtime/topic.hpp"
#include "xrobot/runtime/type_support.hpp"

namespace xrobot::runtime {

enum class PortKind : std::uint8_t {
  kTopicPublisher,
  kTopicSubscriber,
  kServiceClient,
  kServiceServer,
  kActionClient,
  kActionServer,
};

class PortResolver {
 public:
  virtual ~PortResolver() = default;
  virtual Status Resolve(std::string_view name, PortKind kind,
                         SchemaHash schema_hash,
                         void*& endpoint) const noexcept = 0;
};

template <std::size_t Capacity>
class StaticPortRegistry final : public PortResolver {
 public:
  static_assert(Capacity > 0);

  template <MessageType Message>
  Status AddTopicPublisher(std::string_view name,
                           TopicSource<Message>& source) noexcept {
    return Add(name, PortKind::kTopicPublisher,
               TypeSupport<Message>::descriptor().schema_hash,
               static_cast<TopicSource<Message>*>(&source));
  }

  template <MessageType Message>
  Status AddTopicSubscriber(
      std::string_view name,
      TopicSubscriberEndpoint<Message>& subscriber) noexcept {
    return Add(name, PortKind::kTopicSubscriber,
               TypeSupport<Message>::descriptor().schema_hash,
               static_cast<TopicSubscriberEndpoint<Message>*>(&subscriber));
  }

  template <ServiceType Service>
  Status AddServiceClient(std::string_view name,
                          ServiceEndpoint<Service>& endpoint) noexcept {
    return Add(name, PortKind::kServiceClient,
               ServiceTypeSupport<Service>::descriptor().schema_hash,
               static_cast<ServiceEndpoint<Service>*>(&endpoint));
  }

  template <ServiceType Service>
  Status AddServiceServer(
      std::string_view name,
      ServiceServerEndpoint<Service>& endpoint) noexcept {
    return Add(name, PortKind::kServiceServer,
               ServiceTypeSupport<Service>::descriptor().schema_hash,
               static_cast<ServiceServerEndpoint<Service>*>(&endpoint));
  }

  template <ActionType Action>
  Status AddActionClient(std::string_view name,
                         ActionClientEndpoint<Action>& endpoint) noexcept {
    return Add(name, PortKind::kActionClient,
               ActionTypeSupport<Action>::descriptor().schema_hash,
               static_cast<ActionClientEndpoint<Action>*>(&endpoint));
  }

  template <ActionType Action>
  Status AddActionServer(std::string_view name,
                         ActionServerEndpoint<Action>& endpoint) noexcept {
    return Add(name, PortKind::kActionServer,
               ActionTypeSupport<Action>::descriptor().schema_hash,
               static_cast<ActionServerEndpoint<Action>*>(&endpoint));
  }

  Status Seal() noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  Status Resolve(std::string_view name, PortKind kind, SchemaHash schema_hash,
                 void*& endpoint) const noexcept override {
    endpoint = nullptr;
    if (!sealed_) {
      return Status::kInvalidState;
    }

    bool name_found = false;
    for (std::size_t index = 0; index < size_; ++index) {
      const auto& entry = entries_[index];
      if (entry.name != name) {
        continue;
      }
      name_found = true;
      if (entry.kind != kind) {
        continue;
      }
      if (entry.schema_hash != schema_hash) {
        return Status::kTypeMismatch;
      }
      endpoint = entry.endpoint;
      return Status::kOk;
    }
    return name_found ? Status::kTypeMismatch : Status::kUnavailable;
  }

 private:
  struct Entry {
    std::string_view name;
    PortKind kind{PortKind::kTopicPublisher};
    SchemaHash schema_hash{};
    void* endpoint{};
  };

  Status Add(std::string_view name, PortKind kind, SchemaHash schema_hash,
             void* endpoint) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (name.empty() || endpoint == nullptr) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name && entries_[index].kind == kind) {
        return Status::kInvalidArgument;
      }
    }
    if (size_ == Capacity) {
      return Status::kCapacityExceeded;
    }
    entries_[size_++] = Entry{name, kind, schema_hash, endpoint};
    return Status::kOk;
  }

  std::array<Entry, Capacity> entries_{};
  std::size_t size_{};
  bool sealed_{};
};

}  // namespace xrobot::runtime
