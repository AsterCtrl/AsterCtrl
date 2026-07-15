#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>

#include "xrobot/runtime/status.hpp"

namespace xrobot::runtime {

template <typename Device>
concept HardwareDevice = requires {
  { Device::TypeName() } -> std::convertible_to<std::string_view>;
};

class HardwareResolver {
 public:
  virtual ~HardwareResolver() = default;

  virtual Status Resolve(std::string_view name, std::string_view type,
                         void*& device) const noexcept = 0;
};

template <std::size_t Capacity>
class StaticHardwareRegistry final : public HardwareResolver {
 public:
  static_assert(Capacity > 0);

  template <HardwareDevice Device>
  Status Add(std::string_view name, Device& device) noexcept {
    return AddEntry(name, Device::TypeName(), static_cast<void*>(&device));
  }

  Status Seal() noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  Status Resolve(std::string_view name, std::string_view type,
                 void*& device) const noexcept override {
    device = nullptr;
    if (!sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      const auto& entry = entries_[index];
      if (entry.name != name) {
        continue;
      }
      if (entry.type != type) {
        return Status::kTypeMismatch;
      }
      device = entry.device;
      return Status::kOk;
    }
    return Status::kUnavailable;
  }

 private:
  struct Entry {
    std::string_view name;
    std::string_view type;
    void* device{};
  };

  Status AddEntry(std::string_view name, std::string_view type,
                  void* device) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (name.empty() || type.empty() || device == nullptr) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name) {
        return Status::kInvalidArgument;
      }
    }
    if (size_ == Capacity) {
      return Status::kCapacityExceeded;
    }
    entries_[size_++] = {name, type, device};
    return Status::kOk;
  }

  std::array<Entry, Capacity> entries_{};
  std::size_t size_{};
  bool sealed_{};
};

}  // namespace xrobot::runtime
