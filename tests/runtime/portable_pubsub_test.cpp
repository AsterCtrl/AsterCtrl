#include "examples/common/portable_pubsub.hpp"

#include <cassert>

int main() {
  aster::examples::PortablePubSubComposition composition;
  assert(composition.Run() == aster::Status::kOk);
  assert(composition.sink().received());
  assert(composition.sink().sequence() == 42);
  composition.Shutdown();
  assert(composition.state() == aster::RuntimeState::kStopped);
}
