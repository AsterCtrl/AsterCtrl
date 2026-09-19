#include "aster_runtime/execution.hpp"

#include <zephyr/kernel.h>

namespace aster {
ExecutionContext NativeExecutionContext() noexcept {
  const bool interrupt = k_is_in_isr();
  return {interrupt ? "isr" : "unmanaged",
          interrupt ? ExecutionKind::kInterrupt : ExecutionKind::kThread,
          k_ticks_to_ns_floor64(static_cast<std::uint64_t>(k_uptime_ticks()))};
}
}  // namespace aster
