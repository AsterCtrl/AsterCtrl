#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

#include "aster_module_cpp_interface/execution.hpp"
#include "aster_module_cpp_interface/status.hpp"

namespace aster {

class ExecutorRef {
 public:
  constexpr ExecutorRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_executor_base_t*>;
    }
  explicit ExecutorRef(Backend& value) noexcept : ExecutorRef(value.NativeHandle()) {}
  constexpr explicit ExecutorRef(const aster_executor_base_t* executor) noexcept
      : abi_(executor != nullptr && executor->struct_size >= sizeof(*executor) ? executor
                                                                               : nullptr) {}

  [[nodiscard]] std::string_view Name() const noexcept {
    aster_string_view_t name{};
    return abi_ != nullptr && abi_->get_name != nullptr &&
                   abi_->get_name(abi_->impl, &name) == ASTER_STATUS_OK
               ? (name.data == nullptr ? std::string_view{}
                                       : std::string_view(name.data, name.size))
               : std::string_view{};
  }
  Status TryPost(WorkItem work) const noexcept {
    if (abi_ == nullptr || abi_->try_post == nullptr) return Status::kUnavailable;
    if (!work) return Status::kInvalidArgument;
    return FromAbiStatus(abi_->try_post(abi_->impl, work.callback, work.state, nullptr));
  }
  Status TryPostAt(std::uint64_t timestamp, WorkItem work) const noexcept {
    if (abi_ == nullptr || abi_->try_post_at == nullptr) return Status::kUnavailable;
    if (!work) return Status::kInvalidArgument;
    return FromAbiStatus(
        abi_->try_post_at(abi_->impl, timestamp, work.callback, work.state, nullptr));
  }

  Status TryPost(WorkItem work, const ExecutionContext& caller) const noexcept {
    if (abi_ == nullptr || abi_->try_post == nullptr || !work) {
      return abi_ == nullptr ? Status::kUnavailable : Status::kInvalidArgument;
    }
    const auto context = ToAbiExecutionContext(caller);
    return FromAbiStatus(abi_->try_post(abi_->impl, work.callback, work.state, &context));
  }
  Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                   const ExecutionContext& caller) const noexcept {
    if (abi_ == nullptr || abi_->try_post_at == nullptr || !work) {
      return abi_ == nullptr ? Status::kUnavailable : Status::kInvalidArgument;
    }
    const auto context = ToAbiExecutionContext(caller);
    return FromAbiStatus(
        abi_->try_post_at(abi_->impl, timestamp_ns, work.callback, work.state, &context));
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  [[nodiscard]] constexpr const aster_executor_base_t* NativeHandle() const noexcept {
    return abi_;
  }

 private:
  const aster_executor_base_t* abi_{};
};

}  // namespace aster
