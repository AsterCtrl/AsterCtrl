#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

#include "aster_module_cpp_interface/core_ref.hpp"
#include "aster_runtime/core/configurator.hpp"
#include "aster_runtime/core/parameter.hpp"
#include "aster_runtime/registry.hpp"

namespace aster {

/// Fixed-capacity immutable configuration; keys must outlive this registry.
template <std::size_t MaxEntries, std::size_t MaxValueSize>
class StaticConfigurator final : public Configurator, public Registry {
 public:
  static_assert(MaxEntries > 0);
  Status Put(std::string_view key, ValueView value) noexcept {
    if (sealed_) return Status::kInvalidState;
    if (key.empty()) return Status::kInvalidArgument;
    for (std::size_t index = 0; index < size_; ++index)
      if (entries_[index].key == key) return Status::kAlreadyExists;
    if (size_ == entries_.size()) return Status::kCapacityExceeded;
    auto& entry = entries_[size_];
    const auto status = value.CopyTo(entry.storage, entry.value);
    if (!IsOk(status)) return status;
    entry.key = key;
    ++size_;
    return Status::kOk;
  }
  template <ConfigValueType T>
  Status Put(std::string_view key, T value) noexcept {
    return Put(key, ValueView(value));
  }

  Status Get(std::string_view key, ValueView& output) const noexcept override {
    if (!sealed_) return Status::kInvalidState;
    for (std::size_t index = 0; index < size_; ++index) {
      if (entries_[index].key == key) {
        output = entries_[index].value;
        return Status::kOk;
      }
    }
    return Status::kNotFound;
  }
  Status Seal() noexcept override {
    if (sealed_) return Status::kInvalidState;
    sealed_ = true;
    return Status::kOk;
  }
  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }

 private:
  struct Entry {
    std::string_view key;
    ValueView value;
    std::array<std::byte, MaxValueSize> storage{};
  };
  std::array<Entry, MaxEntries> entries_{};
  std::size_t size_{};
  bool sealed_{};
};

/// Fixed storage for a serialized executor. Concurrent runtimes supply a synchronized store.
template <std::size_t MaxEntries, std::size_t MaxValueSize>
class StaticParameterStore final : public ParameterStore, public Registry {
 public:
  static_assert(MaxEntries > 0);
  Status Register(std::string_view name, ValueView value, bool writable = true) noexcept {
    if (sealed_) return Status::kInvalidState;
    if (name.empty()) return Status::kInvalidArgument;
    if (Find(name) != nullptr) return Status::kAlreadyExists;
    if (size_ == entries_.size()) return Status::kCapacityExceeded;
    auto& entry = entries_[size_];
    const auto status = value.CopyTo(entry.storage, entry.value);
    if (!IsOk(status)) return status;
    entry.name = name;
    entry.writable = writable;
    ++size_;
    return Status::kOk;
  }
  Status Get(std::string_view name, ValueView& output,
             std::span<std::byte> storage) const noexcept override {
    if (!sealed_) return Status::kInvalidState;
    const auto* entry = Find(name);
    return entry == nullptr ? Status::kNotFound : entry->value.CopyTo(storage, output);
  }
  Status Set(std::string_view name, ValueView value, const ExecutionContext&) noexcept override {
    if (!sealed_) return Status::kInvalidState;
    auto* entry = Find(name);
    if (entry == nullptr) return Status::kNotFound;
    if (!entry->writable) return Status::kInvalidState;
    if (entry->value.kind() != value.kind()) return Status::kTypeMismatch;
    return value.CopyTo(entry->storage, entry->value);
  }
  Status Seal() noexcept override {
    if (sealed_) return Status::kInvalidState;
    sealed_ = true;
    return Status::kOk;
  }
  [[nodiscard]] bool sealed() const noexcept override { return sealed_; }

 private:
  struct Entry {
    std::string_view name;
    ValueView value;
    std::array<std::byte, MaxValueSize> storage{};
    bool writable{};
  };
  Entry* Find(std::string_view name) noexcept {
    for (std::size_t i = 0; i < size_; ++i)
      if (entries_[i].name == name) return &entries_[i];
    return nullptr;
  }
  const Entry* Find(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < size_; ++i)
      if (entries_[i].name == name) return &entries_[i];
    return nullptr;
  }
  std::array<Entry, MaxEntries> entries_{};
  std::size_t size_{};
  bool sealed_{};
};
}  // namespace aster
