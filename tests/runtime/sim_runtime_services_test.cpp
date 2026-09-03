#include <cassert>

#include "aster/sim/runtime_services.hpp"

int main() {
  aster::sim::ManualClock clock;
  assert(clock.domain() == aster::ClockDomain::kSimulated);
  assert(clock.Advance(100) == aster::Status::kOk);
  assert(clock.NowNs() == 100);
  assert(clock.Set(99) == aster::Status::kInvalidArgument);

  int device = 42;
  aster::sim::FakeHardwareManager<1> hardware;
  assert(hardware.Register("sensor", "example.sensor/v1", &device) == aster::Status::kOk);
  assert(hardware.Seal() == aster::Status::kOk);
  void* resolved{};
  assert(hardware.Resolve("sensor", "example.sensor/v1", resolved) == aster::Status::kOk);
  assert(resolved == &device);
  assert(hardware.Resolve("sensor", "wrong", resolved) == aster::Status::kTypeMismatch);
}
