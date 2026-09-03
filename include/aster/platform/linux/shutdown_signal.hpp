#pragma once

#include <csignal>

#include "aster/status.hpp"

namespace aster::platform::linux {

class ShutdownSignal {
 public:
  ShutdownSignal() noexcept = default;
  ~ShutdownSignal();

  ShutdownSignal(const ShutdownSignal&) = delete;
  ShutdownSignal& operator=(const ShutdownSignal&) = delete;

  Status Install() noexcept;
  void Restore() noexcept;

  [[nodiscard]] bool requested() const noexcept;
  [[nodiscard]] int signal_number() const noexcept;
  [[nodiscard]] bool installed() const noexcept { return installed_; }

 private:
  static void Handle(int signal_number) noexcept;

  static volatile std::sig_atomic_t observed_signal_;
  static bool active_;
  struct sigaction previous_interrupt_ {};
  struct sigaction previous_terminate_ {};
  bool installed_{};
};

}  // namespace aster::platform::linux
