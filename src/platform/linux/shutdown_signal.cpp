#include "aster/platform/linux/shutdown_signal.hpp"

#include <cerrno>
#include <cstring>

namespace aster::platform::linux {

volatile std::sig_atomic_t ShutdownSignal::observed_signal_ = 0;
bool ShutdownSignal::active_ = false;

ShutdownSignal::~ShutdownSignal() { Restore(); }

Status ShutdownSignal::Install() noexcept {
  if (installed_ || active_) {
    return Status::kInvalidState;
  }

  struct sigaction action {};
  action.sa_handler = Handle;
  if (sigemptyset(&action.sa_mask) != 0) {
    return Status::kInternal;
  }
  action.sa_flags = 0;
  observed_signal_ = 0;
  if (sigaction(SIGINT, &action, &previous_interrupt_) != 0) {
    return Status::kUnavailable;
  }
  if (sigaction(SIGTERM, &action, &previous_terminate_) != 0) {
    const auto saved_errno = errno;
    sigaction(SIGINT, &previous_interrupt_, nullptr);
    errno = saved_errno;
    return Status::kUnavailable;
  }
  installed_ = true;
  active_ = true;
  return Status::kOk;
}

void ShutdownSignal::Restore() noexcept {
  if (!installed_) {
    return;
  }
  sigaction(SIGTERM, &previous_terminate_, nullptr);
  sigaction(SIGINT, &previous_interrupt_, nullptr);
  installed_ = false;
  active_ = false;
  observed_signal_ = 0;
}

bool ShutdownSignal::requested() const noexcept { return installed_ && observed_signal_ != 0; }

int ShutdownSignal::signal_number() const noexcept {
  return installed_ ? static_cast<int>(observed_signal_) : 0;
}

void ShutdownSignal::Handle(int signal_number) noexcept { observed_signal_ = signal_number; }

}  // namespace aster::platform::linux
