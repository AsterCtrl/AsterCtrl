#include "aster_runtime/core/allocator.hpp"

#include "validation.hpp"

namespace aster {

Allocator::Allocator() noexcept
    : native_{sizeof(aster_allocator_base_t), this, AllocatorAllocate, AllocatorDeallocate} {}

aster_status_t Allocator::AllocatorAllocate(void* context, std::size_t size, std::size_t alignment,
                                            void** memory) noexcept {
  if (context == nullptr || memory == nullptr || size == 0 || alignment == 0 ||
      (alignment & (alignment - 1U)) != 0) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  auto& allocator = *static_cast<Allocator*>(context);
  *memory = allocator.Allocate(size, alignment);
  return *memory == nullptr ? ASTER_STATUS_CAPACITY_EXCEEDED : ASTER_STATUS_OK;
}

aster_status_t Allocator::AllocatorDeallocate(void* context, void* memory, std::size_t size,
                                              std::size_t alignment) noexcept {
  if (context == nullptr || memory == nullptr || size == 0 || alignment == 0 ||
      (alignment & (alignment - 1U)) != 0) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  auto& allocator = *static_cast<Allocator*>(context);
  allocator.Deallocate(memory, size, alignment);
  return ASTER_STATUS_OK;
}

}  // namespace aster
