#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

#include "aster/core_ref.hpp"
#include "aster/registry.hpp"

namespace aster {

template <std::size_t MaxEntries, std::size_t MaxValueSize>
class StaticConfigurator final : public Configurator, public Registry {
 public:
  static_assert(MaxEntries > 0);
  static_assert(MaxValueSize > 0);

  Status Put(std::string_view key, std::span<const std::byte> value) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (key.empty() || value.empty() || value.size() > MaxValueSize) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].key == key) {
        return Status::kAlreadyExists;
      }
    }
    if (size_ == entries_.size()) {
      return Status::kCapacityExceeded;
    }
    auto& entry = entries_[size_++];
    entry.key = key;
    entry.size = value.size();
    std::copy(value.begin(), value.end(), entry.value.begin());
    return Status::kOk;
  }

  template <typename Value>
    requires std::is_trivially_copyable_v<Value>
  Status Put(std::string_view key, const Value& value) noexcept {
    return Put(key, std::as_bytes(std::span<const Value>(&value, 1)));
  }

  Status Get(std::string_view key, std::span<std::byte> output,
             std::size_t& written) const noexcept override {
    written = 0;
    if (!sealed_) {
      return Status::kInvalidState;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      const auto& entry = entries_[index];
      if (entry.key != key) {
        continue;
      }
      if (output.size() < entry.size) {
        written = entry.size;
        return Status::kCapacityExceeded;
      }
      std::copy_n(entry.value.begin(), entry.size, output.begin());
      written = entry.size;
      return Status::kOk;
    }
    return Status::kNotFound;
  }

  Status Seal() noexcept override {
    if (sealed_) {
      return Status::kInvalidState;
    }
    sealed_ = true;
    return Status::kOk;
  }

  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }

 private:
  struct Entry {
    std::string_view key;
    std::array<std::byte, MaxValueSize> value{};
    std::size_t size{};
  };

  std::array<Entry, MaxEntries> entries_{};
  std::size_t size_{};
  bool sealed_{};
};

template <std::size_t MaxEntries, std::size_t MaxValueSize>
class StaticParameterStore final : public ParameterStore, public Registry {
 public:
  static_assert(MaxEntries > 0);
  static_assert(MaxValueSize > 0);

  Status Register(std::string_view name, std::string_view type,
                  std::span<const std::byte> initial_value, bool writable = true) noexcept {
    if (sealed_) {
      return Status::kInvalidState;
    }
    if (name.empty() || type.empty() || initial_value.empty() ||
        initial_value.size() > MaxValueSize) {
      return Status::kInvalidArgument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name) {
        return Status::kAlreadyExists;
      }
    }
    if (size_ == entries_.size()) {
      return Status::kCapacityExceeded;
    }
    auto& entry = entries_[size_++];
    entry.name = name;
    entry.type = type;
    entry.size = initial_value.size();
    entry.writable = writable;
    std::copy(initial_value.begin(), initial_value.end(), entry.value.begin());
    return Status::kOk;
  }

  Status Get(std::string_view name, std::string_view type, std::span<std::byte> output,
             std::size_t& written) const noexcept override {
    written = 0;
    if (!sealed_) {
      return Status::kInvalidState;
    }
    const auto* entry = Find(name);
    if (entry == nullptr) {
      return Status::kNotFound;
    }
    if (entry->type != type) {
      return Status::kTypeMismatch;
    }
    if (output.size() < entry->size) {
      written = entry->size;
      return Status::kCapacityExceeded;
    }
    std::copy_n(entry->value.begin(), entry->size, output.begin());
    written = entry->size;
    return Status::kOk;
  }

  Status Set(std::string_view name, std::string_view type, std::span<const std::byte> value,
             const ExecutionContext&) noexcept override {
    if (!sealed_) {
      return Status::kInvalidState;
    }
    auto* entry = Find(name);
    if (entry == nullptr) {
      return Status::kNotFound;
    }
    if (entry->type != type) {
      return Status::kTypeMismatch;
    }
    if (!entry->writable) {
      return Status::kInvalidState;
    }
    if (value.empty() || value.size() > entry->value.size()) {
      return Status::kCapacityExceeded;
    }
    std::copy(value.begin(), value.end(), entry->value.begin());
    entry->size = value.size();
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

 private:
  struct Entry {
    std::string_view name;
    std::string_view type;
    std::array<std::byte, MaxValueSize> value{};
    std::size_t size{};
    bool writable{};
  };

  [[nodiscard]] Entry* Find(std::string_view name) noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name) {
        return &entries_[index];
      }
    }
    return nullptr;
  }

  [[nodiscard]] const Entry* Find(std::string_view name) const noexcept {
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].name == name) {
        return &entries_[index];
      }
    }
    return nullptr;
  }

  std::array<Entry, MaxEntries> entries_{};
  std::size_t size_{};
  bool sealed_{};
};

}  // namespace aster
