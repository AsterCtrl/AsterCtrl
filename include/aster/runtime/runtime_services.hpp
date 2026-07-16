#pragma once

#include <cstdint>
#include <string_view>

#include "aster/runtime/execution_context.hpp"
#include "aster/runtime/status.hpp"

namespace aster::runtime {

class SteadyClock {
 public:
  virtual ~SteadyClock() = default;
  virtual std::uint64_t NowNs() const noexcept = 0;
};

enum class LogLevel : std::uint8_t {
  kDebug,
  kInfo,
  kWarning,
  kError,
  kCritical,
};

struct LogRecord {
  std::string_view node_name;
  std::string_view module_name;
  LogLevel level{LogLevel::kInfo};
  std::string_view message;
  std::uint64_t timestamp_ns{};
};

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual Status Write(const LogRecord& record,
                       const ExecutionContext& caller) noexcept = 0;
};

enum class DiagnosticSeverity : std::uint8_t {
  kInfo,
  kWarning,
  kError,
  kFatal,
};

struct DiagnosticRecord {
  std::string_view node_name;
  std::string_view module_name;
  std::string_view name;
  DiagnosticSeverity severity{DiagnosticSeverity::kInfo};
  std::int64_t value{};
  std::uint64_t timestamp_ns{};
};

class DiagnosticSink {
 public:
  virtual ~DiagnosticSink() = default;
  virtual Status Report(const DiagnosticRecord& record,
                        const ExecutionContext& caller) noexcept = 0;
};

}  // namespace aster::runtime
