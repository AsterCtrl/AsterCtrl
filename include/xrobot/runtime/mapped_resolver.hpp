#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "xrobot/runtime/hardware_registry.hpp"
#include "xrobot/runtime/port_registry.hpp"

namespace xrobot::runtime {

struct NameMapping {
  std::string_view local_name;
  std::string_view global_name;
};

class MappedPortResolver final : public PortResolver {
 public:
  constexpr explicit MappedPortResolver(
      std::span<const NameMapping> mappings) noexcept
      : mappings_(mappings) {}

  Status Bind(PortResolver& upstream) noexcept {
    if (upstream_ != nullptr) return Status::kInvalidState;
    if (&upstream == this || !MappingsValid()) {
      return Status::kInvalidArgument;
    }
    upstream_ = &upstream;
    return Status::kOk;
  }

  Status Resolve(std::string_view name, PortKind kind, SchemaHash schema_hash,
                 void*& endpoint) const noexcept override {
    endpoint = nullptr;
    if (upstream_ == nullptr) return Status::kInvalidState;
    for (const auto& mapping : mappings_) {
      if (mapping.local_name == name) {
        return upstream_->Resolve(mapping.global_name, kind, schema_hash,
                                  endpoint);
      }
    }
    return Status::kUnavailable;
  }

  constexpr bool bound() const noexcept { return upstream_ != nullptr; }

 private:
  constexpr bool MappingsValid() const noexcept {
    for (std::size_t index = 0; index < mappings_.size(); ++index) {
      if (mappings_[index].local_name.empty() ||
          mappings_[index].global_name.empty()) {
        return false;
      }
      for (std::size_t previous = 0; previous < index; ++previous) {
        if (mappings_[previous].local_name == mappings_[index].local_name) {
          return false;
        }
      }
    }
    return true;
  }

  std::span<const NameMapping> mappings_;
  PortResolver* upstream_{};
};

class MappedHardwareResolver final : public HardwareResolver {
 public:
  constexpr explicit MappedHardwareResolver(
      std::span<const NameMapping> mappings) noexcept
      : mappings_(mappings) {}

  Status Bind(HardwareResolver& upstream) noexcept {
    if (upstream_ != nullptr) return Status::kInvalidState;
    if (&upstream == this || !MappingsValid()) {
      return Status::kInvalidArgument;
    }
    upstream_ = &upstream;
    return Status::kOk;
  }

  Status Resolve(std::string_view name, std::string_view type,
                 void*& device) const noexcept override {
    device = nullptr;
    if (upstream_ == nullptr) return Status::kInvalidState;
    for (const auto& mapping : mappings_) {
      if (mapping.local_name == name) {
        return upstream_->Resolve(mapping.global_name, type, device);
      }
    }
    return Status::kUnavailable;
  }

  constexpr bool bound() const noexcept { return upstream_ != nullptr; }

 private:
  constexpr bool MappingsValid() const noexcept {
    for (std::size_t index = 0; index < mappings_.size(); ++index) {
      if (mappings_[index].local_name.empty() ||
          mappings_[index].global_name.empty()) {
        return false;
      }
      for (std::size_t previous = 0; previous < index; ++previous) {
        if (mappings_[previous].local_name == mappings_[index].local_name) {
          return false;
        }
      }
    }
    return true;
  }

  std::span<const NameMapping> mappings_;
  HardwareResolver* upstream_{};
};

}  // namespace xrobot::runtime
