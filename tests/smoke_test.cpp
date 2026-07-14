#include <cassert>

#include "xrobot/runtime/version.hpp"

int main() {
  assert(!xrobot::runtime::kVersion.empty());
  return 0;
}
