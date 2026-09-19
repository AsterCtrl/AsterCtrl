#pragma once

#include <mutex>

#include "aster_runtime/core/configurator.hpp"
#include "aster_runtime/core/parameter.hpp"
#include "aster_runtime/platform/linux/configuration.hpp"

namespace aster::platform::linux {

class InstanceConfigurator final : public Configurator {
 public:
  explicit InstanceConfigurator(const ConfigValues& values) noexcept : values_(values) {}
  Status Get(std::string_view key, ValueView& output) const noexcept override {
    const auto found = values_.find(key);
    if (found == values_.end()) return Status::kNotFound;
    return ViewConfigValue(found->second, output);
  }

 private:
  const ConfigValues& values_;
};

class InstanceParameters final : public ParameterStore {
 public:
  explicit InstanceParameters(const ConfigValues& values) : values_(values) {}
  Status Get(std::string_view key, ValueView& output,
             std::span<std::byte> storage) const noexcept override {
    const std::lock_guard lock(mutex_);
    const auto found = values_.find(key);
    if (found == values_.end()) return Status::kNotFound;
    ValueView view;
    const auto status = ViewConfigValue(found->second, view);
    return IsOk(status) ? view.CopyTo(storage, output) : status;
  }
  Status Set(std::string_view key, ValueView value, const ExecutionContext&) noexcept override {
    try {
      const std::lock_guard lock(mutex_);
      const auto found = values_.find(key);
      if (found == values_.end()) return Status::kNotFound;
      ValueView current;
      const auto status = ViewConfigValue(found->second, current);
      if (!IsOk(status)) return status;
      if (current.kind() != value.kind()) return Status::kTypeMismatch;
      return std::visit(
          [&](auto& item) -> Status {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>)
              return Status::kOk;
            else if constexpr (std::is_same_v<T, std::string>) {
              std::string_view text;
              const auto status = value.Get(text);
              if (IsOk(status)) item = text;
              return status;
            } else
              return value.Get(item);
          },
          found->second);
    } catch (const std::bad_alloc&) {
      return Status::kCapacityExceeded;
    } catch (...) {
      return Status::kInternal;
    }
  }

 private:
  mutable std::mutex mutex_;
  ConfigValues values_;
};

}  // namespace aster::platform::linux
