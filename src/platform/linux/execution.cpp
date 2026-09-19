#include "aster_runtime/execution.hpp"

#include <chrono>

namespace aster {
ExecutionContext NativeExecutionContext() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return {"unmanaged", ExecutionKind::kThread,
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(now).count())};
}
}  // namespace aster
