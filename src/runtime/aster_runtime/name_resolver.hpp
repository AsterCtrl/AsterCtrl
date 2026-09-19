#pragma once

#include <algorithm>
#include <span>
#include <string_view>

#include "aster_module_cpp_interface/status.hpp"

namespace aster {

struct NameRemap {
  std::string_view from;
  std::string_view to;
};

// Borrowed configuration, shared by Host and bounded Zephyr registrations.
// Names are expanded once, then remapped exactly once (no patterns or chains).
class NameResolver final {
 public:
  constexpr explicit NameResolver(std::string_view name_space = "/",
                                  std::span<const NameRemap> remaps = {}) noexcept
      : namespace_(name_space.empty() ? "/" : name_space), remaps_(remaps) {}

  [[nodiscard]] Status Validate() const noexcept {
    if (namespace_ != "/" && (!ValidName(namespace_) || namespace_.front() != '/'))
      return Status::kInvalidArgument;
    for (std::size_t i = 0; i < remaps_.size(); ++i) {
      const auto& rule = remaps_[i];
      if (!ValidName(rule.from) || !ValidName(rule.to) || rule.from.front() != '/' ||
          rule.to.front() != '/')
        return Status::kInvalidArgument;
      for (std::size_t j = 0; j < i; ++j)
        if (remaps_[j].from == rule.from) return Status::kAlreadyExists;
    }
    return Status::kOk;
  }

  Status Resolve(std::string_view name, std::span<char> storage,
                 std::string_view& output) const noexcept {
    const auto status = Validate();
    if (!IsOk(status)) return status;
    if (!ValidName(name)) return Status::kInvalidArgument;
    const auto prefix = name.front() == '/' ? std::string_view{} : namespace_;
    const bool separator = !prefix.empty() && prefix.back() != '/';
    // Check each part separately to avoid overflow in the sum.
    if (prefix.size() > storage.size() ||
        static_cast<std::size_t>(separator) > storage.size() - prefix.size() ||
        name.size() > storage.size() - prefix.size() - static_cast<std::size_t>(separator))
      return Status::kCapacityExceeded;
    auto next = std::copy(prefix.begin(), prefix.end(), storage.begin());
    if (separator) *next++ = '/';
    std::copy(name.begin(), name.end(), next);
    const std::string_view expanded(storage.data(), prefix.size() + separator + name.size());
    for (const auto& rule : remaps_) {
      if (expanded == rule.from) {
        if (rule.to.size() > storage.size()) return Status::kCapacityExceeded;
        std::copy(rule.to.begin(), rule.to.end(), storage.begin());
        output = {storage.data(), rule.to.size()};
        return Status::kOk;
      }
    }
    output = expanded;
    return Status::kOk;
  }

 private:
  static bool ValidName(std::string_view name) noexcept {
    if (name.empty() || name.back() == '/') return false;
    if (name.front() == '/') name.remove_prefix(1);
    while (!name.empty()) {
      const auto end = name.find('/');
      const auto part = name.substr(0, end);
      if (part.empty() || part == "." || part == "..") return false;
      for (const char c : part)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
          return false;
      if (end == std::string_view::npos) break;
      name.remove_prefix(end + 1);
    }
    return true;
  }

  std::string_view namespace_;
  std::span<const NameRemap> remaps_;
};

}  // namespace aster
