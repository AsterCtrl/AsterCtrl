#pragma once

#include "aster_module_cpp_interface/allocator/allocator.hpp"

namespace aster {

class Allocator {
 public:
  Allocator() noexcept;
  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;
  [[nodiscard]] const aster_allocator_base_t* NativeHandle() const noexcept { return &native_; }
  virtual ~Allocator() = default;
  virtual void* Allocate(std::size_t size, std::size_t alignment) noexcept = 0;
  virtual void Deallocate(void* memory, std::size_t size, std::size_t alignment) noexcept = 0;

 private:
  static aster_status_t AllocatorAllocate(void* context, std::size_t size, std::size_t alignment,
                                          void** memory) noexcept;
  static aster_status_t AllocatorDeallocate(void* context, void* memory, std::size_t size,
                                            std::size_t alignment) noexcept;
  aster_allocator_base_t native_;
};

}  // namespace aster
