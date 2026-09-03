#include "aster/configuration.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

int main() {
  aster::StaticConfigurator<2, 8> config;
  const std::uint32_t answer = 42;
  assert(config.Put("answer", answer) == aster::Status::kOk);
  assert(config.Seal() == aster::Status::kOk);
  std::uint32_t loaded{};
  assert(aster::ConfiguratorRef(config).Get("answer", loaded) == aster::Status::kOk);
  assert(loaded == answer);

  aster::StaticParameterStore<1, 8> parameters;
  const auto initial = std::as_bytes(std::span<const std::uint32_t>(&answer, 1));
  assert(parameters.Register("gain", "u32", initial) == aster::Status::kOk);
  assert(parameters.Seal() == aster::Status::kOk);
  const std::uint32_t next = 7;
  const aster::ExecutionContext caller("test", aster::ExecutionKind::kThread, 0);
  assert(aster::ParameterRef(parameters)
             .Set("gain", "u32", std::as_bytes(std::span<const std::uint32_t>(&next, 1)), caller) ==
         aster::Status::kOk);
  std::array<std::byte, sizeof(next)> output{};
  std::size_t written{};
  assert(aster::ParameterRef(parameters).Get("gain", "u32", output, written) == aster::Status::kOk);
  assert(written == sizeof(next));
}
