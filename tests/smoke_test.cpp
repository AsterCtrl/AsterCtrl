#include <cassert>

#include "aster/runtime/version.hpp"

int main() {
  assert(!aster::runtime::kVersion.empty());
  return 0;
}
