#include "aster_runtime/core/logger.hpp"

#include "aster_runtime/execution.hpp"
#include "validation.hpp"

namespace aster {

Logger::Logger() noexcept : native_{sizeof(aster_logger_base_t), this, LoggerWrite} {}

aster_status_t Logger::LoggerWrite(void* context, std::uint32_t level, aster_string_view_t message,
                                   const aster_execution_context_t* caller) noexcept {
  if (context == nullptr || !detail::ValidView(message) || level > ASTER_LOG_LEVEL_CRITICAL) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ExecutionContext resolved("unmanaged", ExecutionKind::kThread, 0);
  const auto context_status = ResolveCallContext(caller, resolved);
  if (!IsOk(context_status)) {
    return ToAbiStatus(context_status);
  }
  auto& self = *static_cast<Logger*>(context);
  return ToAbiStatus(self.Write(static_cast<LogLevel>(level), FromAbiString(message), resolved));
}

}  // namespace aster
