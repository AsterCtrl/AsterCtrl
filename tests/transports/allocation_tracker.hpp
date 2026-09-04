#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace aster_test {

std::atomic<std::size_t> allocation_count{};

void* Allocate(std::size_t size, std::size_t alignment) {
  allocation_count.fetch_add(1, std::memory_order_relaxed);
  void* memory{};
  if (alignment <= alignof(std::max_align_t)) {
    memory = std::malloc(size);
  } else if (posix_memalign(&memory, alignment, size) != 0) {
    memory = nullptr;
  }
  if (memory == nullptr) {
    throw std::bad_alloc();
  }
  return memory;
}

std::size_t AllocationCount() noexcept { return allocation_count.load(std::memory_order_relaxed); }

}  // namespace aster_test

#if !defined(ASTER_TEST_DISABLE_ALLOCATION_TRACKER)
void* operator new(std::size_t size) {
  return aster_test::Allocate(size, alignof(std::max_align_t));
}
void* operator new[](std::size_t size) {
  return aster_test::Allocate(size, alignof(std::max_align_t));
}
void* operator new(std::size_t size, std::align_val_t alignment) {
  return aster_test::Allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return aster_test::Allocate(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
#endif
