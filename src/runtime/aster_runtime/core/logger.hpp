#pragma once

#include "aster_module_cpp_interface/logger/logger.hpp"

namespace aster {

class Logger {
 public:
  Logger() noexcept;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  [[nodiscard]] const aster_logger_base_t* NativeHandle() const noexcept { return &native_; }
  virtual ~Logger() = default;
  virtual Status Write(LogLevel level, std::string_view message,
                       const ExecutionContext& caller) noexcept = 0;

 private:
  static aster_status_t LoggerWrite(void* context, std::uint32_t level, aster_string_view_t message,
                                    const aster_execution_context_t* caller) noexcept;
  aster_logger_base_t native_;
};

}  // namespace aster
