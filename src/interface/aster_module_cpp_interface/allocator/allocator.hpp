#pragma once

#include <concepts>
#include <cstddef>

#include "aster_module_c_interface/allocator/allocator_base.h"

namespace aster {

class AllocatorRef {
 public:
  constexpr AllocatorRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_allocator_base_t*>;
    }
  explicit AllocatorRef(Backend& value) noexcept : AllocatorRef(value.NativeHandle()) {}
  constexpr explicit AllocatorRef(const aster_allocator_base_t* allocator) noexcept
      : abi_(allocator != nullptr && allocator->struct_size >= sizeof(*allocator) ? allocator
                                                                                  : nullptr) {}

  void* Allocate(std::size_t size, std::size_t alignment) const noexcept {
    void* memory{};
    return abi_ != nullptr && abi_->allocate != nullptr &&
                   abi_->allocate(abi_->impl, size, alignment, &memory) == ASTER_STATUS_OK
               ? memory
               : nullptr;
  }

  void Deallocate(void* memory, std::size_t size, std::size_t alignment) const noexcept {
    if (abi_ != nullptr && abi_->deallocate != nullptr) {
      static_cast<void>(abi_->deallocate(abi_->impl, memory, size, alignment));
    }
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  [[nodiscard]] constexpr const aster_allocator_base_t* NativeHandle() const noexcept {
    return abi_;
  }

 private:
  const aster_allocator_base_t* abi_{};
};

}  // namespace aster
