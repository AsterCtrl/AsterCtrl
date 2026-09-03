#include "aster/platform/linux/shutdown_signal.hpp"

#include <cassert>
#include <csignal>

int main() {
  aster::platform::linux::ShutdownSignal signal;
  assert(signal.Install() == aster::Status::kOk);
  assert(signal.Install() == aster::Status::kInvalidState);
  aster::platform::linux::ShutdownSignal competing;
  assert(competing.Install() == aster::Status::kInvalidState);
  assert(!signal.requested());

  assert(std::raise(SIGTERM) == 0);
  assert(signal.requested());
  assert(signal.signal_number() == SIGTERM);

  signal.Restore();
  assert(!signal.installed());
  assert(!signal.requested());
  assert(signal.signal_number() == 0);
}
