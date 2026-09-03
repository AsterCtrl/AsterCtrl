#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "aster/core_ref.hpp"
#include "aster/registry.hpp"

namespace aster {

template <std::size_t MaxDevices>
class StaticHardwareManager final : public HardwareManager, public Registry {
 public:
  static_assert(MaxDevices > 0);

  Status Register(std::string_view name, std::string_view type, void* device) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (name.empty() || type.empty() || device == nullptr) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (devices_[index].name == name) {
        return Status::kAlreadyExists;
      }
    }
    if (size_ == devices_.size()) {
      return Status::kCapacityExceeded;
    }
    devices_[size_++] = {name, type, device};
    return Status::kOk;
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }

  Status Resolve(std::string_view name, std::string_view type, void*& device) noexcept override {
    device = nullptr;
    if (!sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (devices_[index].name != name) {
        continue;
      }
      if (devices_[index].type != type) {
        return Status::kTypeMismatch;
      }
      device = devices_[index].device;
      return Status::kOk;
    }
    return Status::kNotFound;
  }

 private:
  struct Device {
    std::string_view name;
    std::string_view type;
    void* device{};
  };

  std::array<Device, MaxDevices> devices_{};
  std::size_t size_{};
  bool sealed_{};
};

}  // namespace aster
