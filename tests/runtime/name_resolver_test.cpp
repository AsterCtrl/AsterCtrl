#include "aster_runtime/name_resolver.hpp"

#include <array>
#include <cassert>

int main() {
  using aster::Status;
  const std::array remaps{aster::NameRemap{"/arm/state", "/robot/joints"},
                          aster::NameRemap{"/robot/joints", "/not-transitive"}};
  const aster::NameResolver resolver("/arm", remaps);
  std::array<char, 64> buffer{};
  std::string_view output;
  assert(resolver.Resolve("state", buffer, output) == Status::kOk);
  assert(output == "/robot/joints");
  assert(resolver.Resolve("/state", buffer, output) == Status::kOk && output == "/state");
  assert(resolver.Resolve("state/child", buffer, output) == Status::kOk &&
         output == "/arm/state/child");
  assert(resolver.Resolve("../state", buffer, output) == Status::kInvalidArgument);
  assert(resolver.Resolve("state//child", buffer, output) == Status::kInvalidArgument);
  assert(resolver.Resolve("", buffer, output) == Status::kInvalidArgument);
  output = "unchanged";
  assert(resolver.Resolve("state", std::span<char>(buffer).first(2), output) ==
         Status::kCapacityExceeded);
  assert(output == "unchanged");
  assert(aster::NameResolver{}.Resolve("state", buffer, output) == Status::kOk &&
         output == "/state");
  const std::array duplicates{aster::NameRemap{"/state", "/one"},
                              aster::NameRemap{"/state", "/two"}};
  assert(aster::NameResolver("/", duplicates).Validate() == Status::kAlreadyExists);
  assert(aster::NameResolver("relative").Validate() == Status::kInvalidArgument);
}
